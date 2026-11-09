// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>

// The header/body split for the HTTP engine, factored out so the host tests
// can exercise it (the full engine needs lwIP). Given one accumulated
// buffer that may already hold body bytes — TensorFold writes headers and
// body start in one TCP segment — finds the CRLFCRLF terminator, parses the
// status line and the Server/Content-Length headers, and reports where the
// body starts. The caller's buffer is never modified.
bool splitHeaders(const char* data, size_t len, char* out, size_t out_len,
                  int* status, const char** server, long* content_length,
                  size_t* body_off);
