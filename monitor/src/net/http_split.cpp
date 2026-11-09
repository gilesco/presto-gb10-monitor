// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "http_split.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Splits an accumulated buffer into its header block and its trailing body
// bytes. Line scanning is done on a COPY: the historical implementation
// tokenized the header buffer in place with strtok_r, and once TensorFold
// began writing headers + body start in one TCP segment, that pass rewrote
// every \r in the first body lines to NUL — silently eating the first
// /metrics fields (185 lines / 19 recognised became 178 / 17 on the board,
// and PP read no data all day). Nothing here mutates the caller's buffer.
//
// *server points into out (the caller's copy) and stays valid as long as it
// does; out needs no more space than the header block itself (the engine
// passes its 2 KB header buffer).
bool splitHeaders(const char* data, size_t len, char* out, size_t out_len,
                  int* status, const char** server, long* content_length,
                  size_t* body_off) {
  *status = 0;
  *server = "";
  *content_length = -1;
  *body_off = 0;

  // CRLFCRLF anywhere in the accumulated block: the header block ends here
  // and body bytes may follow in the same segment.
  const char* end = nullptr;
  for (size_t i = 0; i + 4 <= len; i++) {
    if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
      end = data + i + 4;
      break;
    }
  }
  if (!end) return false;  // headers still incomplete

  const size_t hdr_total = (size_t)(end - data);
  const size_t hdr = hdr_total >= out_len ? out_len - 1 : hdr_total;  // buffer cap
  memcpy(out, data, hdr);
  out[hdr] = '\0';

  char* save = nullptr;
  char* line = strtok_r(out, "\r\n", &save);
  if (!line || strncmp(line, "HTTP/", 5) != 0 || !strchr(line, ' ')) return false;
  *status = atoi(strchr(line, ' ') + 1);
  while ((line = strtok_r(nullptr, "\r\n", &save))) {
    if (strncasecmp(line, "Server:", 7) == 0) {
      const char* v = line + 7;
      while (*v == ' ') v++;
      *server = v;
    } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
      *content_length = atol(line + 15);
    }
  }

  *body_off = hdr_total;
  return true;
}
