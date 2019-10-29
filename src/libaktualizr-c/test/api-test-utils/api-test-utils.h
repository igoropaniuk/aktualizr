#ifndef API_TEST_UTILS_H
#define API_TEST_UTILS_H

#ifdef __cplusplus
#include <boost/process.hpp>
#include "config/config.h"

using Config = Config;
using FakeHttpServer = boost::process::child;

extern "C" {
#else
typedef struct Config Config;
typedef struct FakeHttpServer FakeHttpServer;
#endif

FakeHttpServer* Run_fake_http_server(const char* serverPath, const char* metaPath);
void Stop_fake_http_server(FakeHttpServer* server);

Config* Get_test_config(const char* storagePath);
void Remove_test_config(Config* config);

#ifdef __cplusplus
}
#endif

#endif  // API_TEST_UTILS_H
