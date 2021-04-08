#include "bootloader.h"

#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/filesystem.hpp>

#include "storage/invstorage.h"
#include "utilities/exceptions.h"
#include "utilities/utils.h"

Bootloader::Bootloader(BootloaderConfig config, INvStorage& storage) : config_(std::move(config)), storage_(storage) {
  reboot_sentinel_ = config_.reboot_sentinel_dir / config_.reboot_sentinel_name;
  reboot_command_ = config_.reboot_command;

  if (!Utils::createSecureDirectory(config_.reboot_sentinel_dir)) {
    LOG_WARNING << "Could not create " << config_.reboot_sentinel_dir << " securely, reboot detection support disabled";
    reboot_detect_supported_ = false;
    return;
  }

  reboot_detect_supported_ = true;
}

void Bootloader::setBootOK() const {
  std::string sink;
  switch (config_.rollback_mode) {
    case RollbackMode::kBootloaderNone:
      break;
    case RollbackMode::kUbootGeneric:
      if (Utils::shell("fw_setenv bootcount 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting bootcount";
      }
      break;
    case RollbackMode::kUbootMasked:
      if (Utils::shell("fw_setenv bootcount 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting bootcount";
      }
      if (Utils::shell("fw_setenv upgrade_available 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting upgrade_available for u-boot";
      }
      break;
    case RollbackMode::kFioVB:
      if (Utils::shell("fiovb_setenv bootcount 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting bootcount";
      }
      if (Utils::shell("fiovb_setenv upgrade_available 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting upgrade_available";
      }
      break;
    default:
      throw NotImplementedException();
  }
}

void Bootloader::updateNotify() const {
  std::string sink;
  switch (config_.rollback_mode) {
    case RollbackMode::kBootloaderNone:
      break;
    case RollbackMode::kUbootGeneric:
      if (Utils::shell("fw_setenv bootcount 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting bootcount";
      }
      if (Utils::shell("fw_setenv rollback 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting rollback flag";
      }
      break;
    case RollbackMode::kUbootMasked:
      if (Utils::shell("fw_setenv bootcount 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting bootcount";
      }
      if (Utils::shell("fw_setenv upgrade_available 1", &sink) != 0) {
        LOG_WARNING << "Failed setting upgrade_available for u-boot";
      }
      if (Utils::shell("fw_setenv rollback 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting rollback flag";
      }
      break;
    case RollbackMode::kFioVB:
      if (Utils::shell("fiovb_setenv bootcount 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting bootcount";
      }
      if (Utils::shell("fiovb_setenv upgrade_available 1", &sink) != 0) {
        LOG_WARNING << "Failed setting upgrade_available";
      }
      if (Utils::shell("fiovb_setenv rollback 0", &sink) != 0) {
        LOG_WARNING << "Failed resetting rollback flag";
      }
      break;
    default:
      throw NotImplementedException();
  }
}

void Bootloader::installNotify(const Uptane::Target& target) const {
  std::string sink;
  std::string target_hash = target.sha256Hash();
  std::string version_flag = "bootfirmware_version";
  // TODO: provide initial /ostree/deploy/lmp/deploy/ from config
  // instead of hardcoding that here
  std::string version_file = std::string("/ostree/deploy/lmp/deploy/" +
                                         target_hash + ".0"
                                         "/usr/lib/firmware/version.txt");
  std::ifstream t(version_file);
  LOG_INFO << "Reading target boot firmware version file: " << version_file;

  std::string target_firmware_ver((std::istreambuf_iterator<char>(t)),
                                   std::istreambuf_iterator<char>());
  // Drop "bootfirmware_flag=" substring
  std::string::size_type i = target_firmware_ver.find(version_flag);
  if (i != std::string::npos)
    target_firmware_ver.erase(i, version_flag.length() + 1);

  LOG_INFO << "Target boot firmware version: " << target_firmware_ver;

  switch (config_.rollback_mode) {
    case RollbackMode::kBootloaderNone:
      break;
    case RollbackMode::kUbootGeneric:
      break;
    case RollbackMode::kUbootMasked:
      if (Utils::shell("fw_printenv bootfirmware_version", &sink) != 0) {
        LOG_WARNING << "Failed getting bootfirmware_version for u-boot";
      }
      LOG_INFO << "Current boot firmware version: " << sink;
      if (sink.compare(target_firmware_ver) != 0) {
        LOG_INFO << "Update boot firmware to version: " << target_firmware_ver;
        if (Utils::shell("fw_setenv bootupgrade_available 1", &sink) != 0) {
          LOG_WARNING << "Failed setting bootupgrade_available for u-boot";
        }
      } else {
        LOG_INFO << "Update of boot firmware is not needed" << sink;
      }
      break;
    case RollbackMode::kFioVB:
      if (Utils::shell("fiovb_printenv bootfirmware_version", &sink) != 0) {
        LOG_WARNING << "Failed getting bootfirmware_version for u-boot";
      }
      LOG_INFO << "Current boot firmware version: " << sink;
      if (sink.compare(target_firmware_ver) != 0) {
        LOG_INFO << "Update boot firmware to version: " << target_firmware_ver;
        if (Utils::shell("fiovb_setenv bootupgrade_available 1", &sink) != 0) {
          LOG_WARNING << "Failed to set bootupgrade_available";
        }
      } else {
        LOG_INFO << "Update of boot firmware is not needed" << sink;
      }
      break;
    default:
      throw NotImplementedException();
  }
}

bool Bootloader::supportRebootDetection() const { return reboot_detect_supported_; }

bool Bootloader::rebootDetected() const {
  if (!reboot_detect_supported_) {
    return false;
  }

  // true if set in storage and no volatile flag

  bool sentinel_exists = boost::filesystem::exists(reboot_sentinel_);
  bool need_reboot = false;

  storage_.loadNeedReboot(&need_reboot);

  return need_reboot && !sentinel_exists;
}

void Bootloader::rebootFlagSet() {
  if (!reboot_detect_supported_) {
    return;
  }

  // set in storage + volatile flag

  Utils::writeFile(reboot_sentinel_, std::string(), false);  // empty file
  storage_.storeNeedReboot();
}

void Bootloader::rebootFlagClear() {
  if (!reboot_detect_supported_) {
    return;
  }

  // clear in storage + volatile flag

  storage_.clearNeedReboot();
  boost::filesystem::remove(reboot_sentinel_);
}

void Bootloader::reboot(bool fake_reboot) {
  if (fake_reboot) {
    boost::filesystem::remove(reboot_sentinel_);
    return;
  }
  if (setuid(0) != 0) {
    LOG_ERROR << "Failed to set/verify a root user so cannot reboot system programmatically";
    return;
  }
  sync();
  if (system(reboot_command_.c_str()) != 0) {
    LOG_ERROR << "Failed to execute the reboot command: " << reboot_command_;
  }
}
