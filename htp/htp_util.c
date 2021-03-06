/***************************************************************************
 * Copyright (c) 2009-2010 Open Information Security Foundation
 * Copyright (c) 2010-2013 Qualys, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.

 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.

 * - Neither the name of the Qualys, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

/**
 * @file
 * @author Ivan Ristic <ivanr@webkreator.com>
 */

#include <ctype.h>

#if defined(__cplusplus) && !defined(__STDC_FORMAT_MACROS)
/* C99 requires that inttypes.h only exposes PRI* macros
 * for C++ implementations if this is defined: */
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <stdarg.h>

#include "htp.h"
#include "htp_private.h"
#include "htp_utf8_decoder.h"

/**
 * Is character a linear white space character?
 *
 * @param[in] c
 * @return 0 or 1
 */
int htp_is_lws(int c) {
    if ((c == ' ') || (c == '\t')) return 1;
    else return 0;
}

/**
 * Is character a separator character?
 *
 * @param[in] c
 * @return 0 or 1
 */
int htp_is_separator(int c) {
    /* separators = "(" | ")" | "<" | ">" | "@"
                  | "," | ";" | ":" | "\" | <">
                  | "/" | "[" | "]" | "?" | "="
                  | "{" | "}" | SP | HT         */
    switch (c) {
        case '(':
        case ')':
        case '<':
        case '>':
        case '@':
        case ',':
        case ';':
        case ':':
        case '\\':
        case '"':
        case '/':
        case '[':
        case ']':
        case '?':
        case '=':
        case '{':
        case '}':
        case ' ':
        case '\t':
            return 1;
            break;
        default:
            return 0;
    }
}

/**
 * Is character a text character?
 *
 * @param[in] c
 * @return 0 or 1
 */
int htp_is_text(int c) {
    if (c == '\t') return 1;
    if (c < 32) return 0;
    return 1;
}

/**
 * Is character a token character?
 *
 * @param[in] c
 * @return 0 or 1
 */
int htp_is_token(int c) {
    /* token = 1*<any CHAR except CTLs or separators> */
    /* CHAR  = <any US-ASCII character (octets 0 - 127)> */
    if ((c < 32) || (c > 126)) return 0;
    if (htp_is_separator(c)) return 0;
    return 1;
}

/**
 * Remove one or more line terminators (LF or CRLF) from
 * the end of the line provided as input.
 *
 * @return 0 if nothing was removed, 1 if one or more LF characters were removed, or
 *         2 if one or more CR and/or LF characters were removed.
 */
int htp_chomp(unsigned char *data, size_t *len) {
    int r = 0;

    // Loop until there's no more stuff in the buffer
    while (*len > 0) {
        // Try one LF first
        if (data[*len - 1] == LF) {
            (*len)--;
            r = 1;

            if (*len == 0) return r;

            // A CR is allowed before LF
            if (data[*len - 1] == CR) {
                (*len)--;
                r = 2;
            }
        } else return r;
    }

    return r;
}

/**
 * Is character a white space character?
 *
 * @param[in] c
 * @return 0 or 1
 */
int htp_is_space(int c) {
    switch (c) {
        case ' ':
        case '\f':
        case '\v':
        case '\t':
        case '\r':
        case '\n':
            return 1;
        default:
            return 0;
    }
}

/**
 * Converts request method, given as a string, into a number.
 *
 * @param[in] method
 * @return Method number of M_UNKNOWN
 */
int htp_convert_method_to_number(bstr *method) {
    if (method == NULL) return HTP_M_UNKNOWN;

    // TODO Optimize using parallel matching, or something similar.

    if (bstr_cmp_c(method, "GET") == 0) return HTP_M_GET;
    if (bstr_cmp_c(method, "PUT") == 0) return HTP_M_PUT;
    if (bstr_cmp_c(method, "POST") == 0) return HTP_M_POST;
    if (bstr_cmp_c(method, "DELETE") == 0) return HTP_M_DELETE;
    if (bstr_cmp_c(method, "CONNECT") == 0) return HTP_M_CONNECT;
    if (bstr_cmp_c(method, "OPTIONS") == 0) return HTP_M_OPTIONS;
    if (bstr_cmp_c(method, "TRACE") == 0) return HTP_M_TRACE;
    if (bstr_cmp_c(method, "PATCH") == 0) return HTP_M_PATCH;
    if (bstr_cmp_c(method, "PROPFIND") == 0) return HTP_M_PROPFIND;
    if (bstr_cmp_c(method, "PROPPATCH") == 0) return HTP_M_PROPPATCH;
    if (bstr_cmp_c(method, "MKCOL") == 0) return HTP_M_MKCOL;
    if (bstr_cmp_c(method, "COPY") == 0) return HTP_M_COPY;
    if (bstr_cmp_c(method, "MOVE") == 0) return HTP_M_MOVE;
    if (bstr_cmp_c(method, "LOCK") == 0) return HTP_M_LOCK;
    if (bstr_cmp_c(method, "UNLOCK") == 0) return HTP_M_UNLOCK;
    if (bstr_cmp_c(method, "VERSION_CONTROL") == 0) return HTP_M_VERSION_CONTROL;
    if (bstr_cmp_c(method, "CHECKOUT") == 0) return HTP_M_CHECKOUT;
    if (bstr_cmp_c(method, "UNCHECKOUT") == 0) return HTP_M_UNCHECKOUT;
    if (bstr_cmp_c(method, "CHECKIN") == 0) return HTP_M_CHECKIN;
    if (bstr_cmp_c(method, "UPDATE") == 0) return HTP_M_UPDATE;
    if (bstr_cmp_c(method, "LABEL") == 0) return HTP_M_LABEL;
    if (bstr_cmp_c(method, "REPORT") == 0) return HTP_M_REPORT;
    if (bstr_cmp_c(method, "MKWORKSPACE") == 0) return HTP_M_MKWORKSPACE;
    if (bstr_cmp_c(method, "MKACTIVITY") == 0) return HTP_M_MKACTIVITY;
    if (bstr_cmp_c(method, "BASELINE_CONTROL") == 0) return HTP_M_BASELINE_CONTROL;
    if (bstr_cmp_c(method, "MERGE") == 0) return HTP_M_MERGE;
    if (bstr_cmp_c(method, "INVALID") == 0) return HTP_M_INVALID;
    if (bstr_cmp_c(method, "HEAD") == 0) return HTP_M_HEAD;

    return HTP_M_UNKNOWN;
}

/**
 * Is the given line empty? This function expects the line to have
 * a terminating LF.
 *
 * @param[in] data
 * @param[in] len
 * @return 0 or 1
 */
int htp_is_line_empty(unsigned char *data, size_t len) {
    if ((len == 1) || ((len == 2) && (data[0] == CR))) {
        return 1;
    }

    return 0;
}

/**
 * Does line consist entirely of whitespace characters?
 * 
 * @param[in] data
 * @param[in] len
 * @return 0 or 1
 */
int htp_is_line_whitespace(unsigned char *data, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        if (!isspace(data[i])) {
            return 0;
        }
    }

    return 1;
}

/**
 * Parses Content-Length string (positive decimal number).
 * White space is allowed before and after the number.
 *
 * @param[in] b
 * @return Content-Length as a number, or -1 on error.
 */
int64_t htp_parse_content_length(bstr *b) {
    return htp_parse_positive_integer_whitespace((unsigned char *) bstr_ptr(b), bstr_len(b), 10);
}

/**
 * Parses chunk length (positive hexadecimal number).
 * White space is allowed before and after the number.
 *
 * @param[in] data
 * @param[in] len
 * @return Chunk length, or -1 on error.
 */
int64_t htp_parse_chunked_length(unsigned char *data, size_t len) {
    int64_t chunk_len = htp_parse_positive_integer_whitespace(data, len, 16);
    if (chunk_len < 0) return chunk_len;
    if (chunk_len > SIZE_MAX) return -1;
    return chunk_len;
}

/**
 * A somewhat forgiving parser for a positive integer in a given base.
 * Only LWS is allowed before and after the number.
 * 
 * @param[in] data
 * @param[in] len
 * @param[in] base
 * @return The parsed number on success; a negative number on error.
 */
int64_t htp_parse_positive_integer_whitespace(unsigned char *data, size_t len, int base) {
    if (len == 0) return -1003;

    size_t last_pos;
    size_t pos = 0;

    // Ignore LWS before
    while ((pos < len) && (htp_is_lws(data[pos]))) pos++;
    if (pos == len) return -1001;

    int64_t r = bstr_util_mem_to_pint(data + pos, len - pos, base, &last_pos);
    if (r < 0) return r;

    // Move after the last digit
    pos += last_pos;

    // Ignore LWS after
    while (pos < len) {
        if (!htp_is_lws(data[pos])) {
            return -1002;
        }

        pos++;
    }

    return r;
}

/**
 * Prints one log message to stderr.
 * 
 * @param[in] log
 */
void htp_print_log(FILE *stream, htp_log_t *log) {
    if (log->code != 0) {
        fprintf(stream, "[%d][code %d][file %s][line %d] %s\n", log->level,
                log->code, log->file, log->line, log->msg);
    } else {
        fprintf(stream, "[%d][file %s][line %d] %s\n", log->level,
                log->file, log->line, log->msg);
    }
}

/**
 * Records one log message.
 * 
 * @param[in] connp
 * @param[in] file
 * @param[in] line
 * @param[in] level
 * @param[in] code
 * @param[in] fmt
 */
void htp_log(htp_connp_t *connp, const char *file, int line, enum htp_log_level_t level, int code, const char *fmt, ...) {
    if (connp == NULL) return;

    char buf[1024];
    va_list args;

    // Ignore messages below our log level
    if (connp->cfg->log_level < level) {
        return;
    }

    va_start(args, fmt);

    int r = vsnprintf(buf, 1023, fmt, args);

    va_end(args);

    if (r < 0) {
        snprintf(buf, 1024, "[vnsprintf returned error %d]", r);
    }

    // Indicate overflow with a '+' at the end
    if (r > 1023) {
        buf[1022] = '+';
        buf[1023] = '\0';
    }

    // Create a new log entry...
    htp_log_t *log = calloc(1, sizeof (htp_log_t));
    if (log == NULL) return;

    log->connp = connp;
    log->file = file;
    log->line = line;
    log->level = level;
    log->code = code;
    log->msg = strdup(buf);

    htp_list_add(connp->conn->messages, log);

    if (level == HTP_LOG_ERROR) {
        connp->last_error = log;
    }

    htp_hook_run_all(connp->cfg->hook_log, log);
}

/**
 * Determines if the given line is a continuation (of some previous line).
 *
 * @param[in] connp
 * @param[in] data
 * @param[in] len
 * @return 0 or 1
 */
int htp_connp_is_line_folded(unsigned char *data, size_t len) {
    // Is there a line?
    if (len == 0) {
        return -1;
    }

    if (htp_is_lws(data[0])) return 1;
    else return 0;
}

/**
 * Determines if the given line is a request terminator.
 *
 * @param[in] connp
 * @param[in] data
 * @param[in] len
 * @return 0 or 1
 */
int htp_connp_is_line_terminator(htp_connp_t *connp, unsigned char *data, size_t len) {
    // Is this the end of request headers?
    switch (connp->cfg->server_personality) {
        case HTP_SERVER_IIS_5_1:
            // IIS 5 will accept a whitespace line as a terminator
            if (htp_is_line_whitespace(data, len)) {
                return 1;
            }

            // Fall through
        default:
            // Treat an empty line as terminator
            if (htp_is_line_empty(data, len)) {
                return 1;
            }
            break;
    }

    return 0;
}

/**
 * Determines if the given line can be ignored when it appears before a request.
 *
 * @param[in] connp
 * @param[in] data
 * @param[in] len
 * @return 0 or 1
 */
int htp_connp_is_line_ignorable(htp_connp_t *connp, unsigned char *data, size_t len) {
    return htp_connp_is_line_terminator(connp, data, len);
}

/**
 * Parses an authority string, which consists of a hostname with an optional port number; username
 * and password are not allowed and will not be handled. On success, this function will allocate a
 * new bstring, which the caller will need to manage. If the port information is not available or
 * if it is invalid, the port variable will contain -1. The HTP_HOST_INVALID flag will be set if
 * the authority is in the incorrect format.
 *
 * @param[in] hostport
 * @param[in,out] hostname
 * @param[in,out] port
 * @param[in,out] flags
 * @return HTP_OK on success, HTP_ERROR on failure.
 */
htp_status_t htp_parse_hostport(bstr *hostport, bstr **hostname, int *port, uint64_t *flags) {
    if ((hostport == NULL) || (hostname == NULL) || (port == NULL) || (flags == NULL)) return HTP_ERROR;

    unsigned char *data = bstr_ptr(hostport);
    size_t len = bstr_len(hostport);

    // Ignore whitespace at the beginning and the end.
    bstr_util_mem_trim(&data, &len);

    // Is there a colon?
    unsigned char *colon = memchr(data, ':', len);
    if (colon == NULL) {
        // Hostname alone, no port.

        *port = -1;

        // Ignore one dot at the end.
        if ((len > 0) && (data[len - 1] == '.')) len--;

        *hostname = bstr_dup_mem(data, len);
        if (*hostname == NULL) return HTP_ERROR;

        bstr_to_lowercase(*hostname);
    } else {
        // Hostname and port.

        // Ignore whitespace at the end of hostname.
        unsigned char *hostend = colon;
        while ((hostend > data) && (isspace(*(hostend - 1)))) hostend--;

        // Ignore one dot at the end.
        if ((hostend > data) && (*(hostend - 1) == '.')) hostend--;

        *hostname = bstr_dup_mem(data, hostend - data);
        if (*hostname == NULL) return HTP_ERROR;

        bstr_to_lowercase(*hostname);

        // Parse the port.

        unsigned char *portstart = colon + 1;
        size_t portlen = len - (portstart - data);

        int64_t port_parsed = htp_parse_positive_integer_whitespace(portstart, portlen, 10);

        if (port_parsed < 0) {
            // Failed to parse the port number.
            *port = -1;
            *flags |= HTP_HOST_INVALID;
        } else if ((port_parsed > 0) && (port_parsed < 65536)) {
            // Valid port number.
            *port = port_parsed;
        } else {
            // Port number out of range.
            *port = -1;
            *flags |= HTP_HOST_INVALID;
        }
    }

    return HTP_OK;
}

/**
 * Parses request URI, making no attempt to validate the contents.
 *
 * @param[in] connp
 * @param[in] authority
 * @param[in] uri
 * @return HTP_ERROR on memory allocation failure, HTP_OK otherwise
 */
int htp_parse_uri_hostport(htp_connp_t *connp, bstr *hostport, htp_uri_t **uri) {
    return htp_parse_hostport(hostport, &((*uri)->hostname), &((*uri)->port_number), &(connp->in_tx->flags));
}

/**
 * Parses request URI, making no attempt to validate the contents.
 * 
 * @param[in] input
 * @param[in] uri
 * @return HTP_ERROR on memory allocation failure, HTP_OK otherwise
 */
int htp_parse_uri(bstr *input, htp_uri_t **uri) {
    if (input == NULL) return HTP_ERROR;

    unsigned char *data = bstr_ptr(input);
    size_t len = bstr_len(input);
    size_t start, pos;

    // Allow a htp_uri_t structure to be provided on input,
    // but allocate a new one if there isn't one
    if (*uri == NULL) {
        *uri = calloc(1, sizeof (htp_uri_t));
        if (*uri == NULL) return HTP_ERROR;
    }

    if (len == 0) {
        // Empty string
        return HTP_OK;
    }

    pos = 0;

    // Scheme test: if it doesn't start with a forward slash character (which it must
    // for the contents to be a path or an authority, then it must be the scheme part
    if (data[0] != '/') {
        // Parse scheme        

        // Find the colon, which marks the end of the scheme part
        start = pos;
        while ((pos < len) && (data[pos] != ':')) pos++;

        if (pos >= len) {
            // We haven't found a colon, which means that the URI
            // is invalid. Apache will ignore this problem and assume
            // the URI contains an invalid path so, for the time being,
            // we are going to do the same.
            pos = 0;
        } else {
            // Make a copy of the scheme
            (*uri)->scheme = bstr_dup_mem(data + start, pos - start);

            // Go over the colon
            pos++;
        }
    }

    // Authority test: two forward slash characters and it's an authority.
    // One, three or more slash characters, and it's a path. We, however,
    // only attempt to parse authority if we've seen a scheme.
    if ((*uri)->scheme != NULL)
        if ((pos + 2 < len) && (data[pos] == '/') && (data[pos + 1] == '/') && (data[pos + 2] != '/')) {
            // Parse authority

            // Go over the two slash characters
            start = pos = pos + 2;

            // Authority ends with a question mark, forward slash or hash
            while ((pos < len) && (data[pos] != '?') && (data[pos] != '/') && (data[pos] != '#')) pos++;

            unsigned char *hostname_start;
            size_t hostname_len;

            // Are the credentials included in the authority?
            unsigned char *m = memchr(data + start, '@', pos - start);
            if (m != NULL) {
                // Credentials present
                unsigned char *credentials_start = data + start;
                size_t credentials_len = m - data - start;

                // Figure out just the hostname part
                hostname_start = data + start + credentials_len + 1;
                hostname_len = pos - start - credentials_len - 1;

                // Extract the username and the password
                m = memchr(credentials_start, ':', credentials_len);
                if (m != NULL) {
                    // Username and password
                    (*uri)->username = bstr_dup_mem(credentials_start, m - credentials_start);
                    (*uri)->password = bstr_dup_mem(m + 1, credentials_len - (m - credentials_start) - 1);
                } else {
                    // Username alone
                    (*uri)->username = bstr_dup_mem(credentials_start, credentials_len);
                }
            } else {
                // No credentials
                hostname_start = data + start;
                hostname_len = pos - start;
            }

            // Still parsing authority; is there a port provided?
            m = memchr(hostname_start, ':', hostname_len);
            if (m != NULL) {
                size_t port_len = hostname_len - (m - hostname_start) - 1;
                hostname_len = hostname_len - port_len - 1;

                // Port string
                (*uri)->port = bstr_dup_mem(m + 1, port_len);

                // We deliberately don't want to try to convert the port
                // string as a number. That will be done later, during
                // the normalization and validation process.
            }

            // Hostname
            (*uri)->hostname = bstr_dup_mem(hostname_start, hostname_len);
        }

    // Path
    start = pos;

    // The path part will end with a question mark or a hash character, which
    // mark the beginning of the query part or the fragment part, respectively.
    while ((pos < len) && (data[pos] != '?') && (data[pos] != '#')) pos++;

    // Path
    (*uri)->path = bstr_dup_mem(data + start, pos - start);

    if (pos == len) return HTP_OK;

    // Query
    if (data[pos] == '?') {
        // Step over the question mark
        start = pos + 1;

        // The query part will end with the end of the input
        // or the beginning of the fragment part
        while ((pos < len) && (data[pos] != '#')) pos++;

        // Query string
        (*uri)->query = bstr_dup_mem(data + start, pos - start);

        if (pos == len) return HTP_OK;
    }

    // Fragment
    if (data[pos] == '#') {
        // Step over the hash character
        start = pos + 1;

        // Fragment; ends with the end of the input
        (*uri)->fragment = bstr_dup_mem(data + start, len - start);
    }

    return HTP_OK;
}

/**
 * Convert two input bytes, pointed to by the pointer parameter,
 * into a single byte by assuming the input consists of hexadecimal
 * characters. This function will happily convert invalid input.
 *
 * @param[in] what
 * @return hex-decoded byte
 */
static unsigned char x2c(unsigned char *what) {
    register unsigned char digit;

    digit = (what[0] >= 'A' ? ((what[0] & 0xdf) - 'A') + 10 : (what[0] - '0'));
    digit *= 16;
    digit += (what[1] >= 'A' ? ((what[1] & 0xdf) - 'A') + 10 : (what[1] - '0'));

    return digit;
}

/**
 * Convert a Unicode codepoint into a single-byte, using best-fit
 * mapping (as specified in the provided configuration structure).
 *
 * @param[in] cfg
 * @param[in] codepoint
 * @return converted single byte
 */
static uint8_t bestfit_codepoint(htp_cfg_t *cfg, uint32_t codepoint) {
    // Is it a single-byte codepoint?
    if (codepoint < 0x100) {
        return (uint8_t) codepoint;
    }

    // Our current implementation only converts the 2-byte codepoints
    if (codepoint > 0xffff) {
        return cfg->bestfit_replacement_char;
    }

    uint8_t *p = cfg->bestfit_map;

    // TODO Optimize lookup.

    for (;;) {
        uint32_t x = (p[0] << 8) + p[1];

        if (x == 0) {
            return cfg->bestfit_replacement_char;
        }

        if (x == codepoint) {
            return p[2];
        }

        // Move to the next triplet
        p += 3;
    }
}

/**
 * Decode a UTF-8 encoded path. Overlong characters will be decoded, invalid
 * characters will be left as-is. Best-fit mapping will be used to convert
 * UTF-8 into a single-byte stream.
 *
 * @param[in] cfg
 * @param[in] tx
 * @param[in] path
 */
void htp_utf8_decode_path_inplace(htp_cfg_t *cfg, htp_tx_t *tx, bstr *path) {
    if (path == NULL) return;

    uint8_t *data = (unsigned char *) bstr_ptr(path);
    size_t len = bstr_len(path);
    size_t rpos = 0;
    size_t wpos = 0;
    size_t charpos = 0;
    uint32_t codepoint = 0;
    uint32_t state = HTP_UTF8_ACCEPT;
    uint32_t counter = 0;
    uint8_t seen_valid = 0;

    while (rpos < len) {
        counter++;

        switch (htp_utf8_decode_allow_overlong(&state, &codepoint, data[rpos])) {
            case HTP_UTF8_ACCEPT:
                if (counter == 1) {
                    // ASCII character
                    data[wpos++] = (uint8_t) codepoint;
                } else {
                    // A valid UTF-8 character
                    seen_valid = 1;

                    // Check for overlong characters and set the
                    // flag accordingly
                    switch (counter) {
                        case 2:
                            if (codepoint < 0x80) {
                                tx->flags |= HTP_PATH_UTF8_OVERLONG;
                            }
                            break;
                        case 3:
                            if (codepoint < 0x800) {
                                tx->flags |= HTP_PATH_UTF8_OVERLONG;
                            }
                            break;
                        case 4:
                            if (codepoint < 0x10000) {
                                tx->flags |= HTP_PATH_UTF8_OVERLONG;
                            }
                            break;
                    }

                    // Special flag for fullwidth form evasion
                    if ((codepoint >= 0xff00) && (codepoint <= 0xffef)) {
                        tx->flags |= HTP_PATH_HALF_FULL_RANGE;
                    }

                    // Use best-fit mapping to convert to a single byte
                    data[wpos++] = bestfit_codepoint(cfg, codepoint);
                }

                // Advance over the consumed byte
                rpos++;

                // Prepare for the next character
                counter = 0;
                charpos = rpos;

                break;

            case HTP_UTF8_REJECT:
                // Invalid UTF-8 character
                tx->flags |= HTP_PATH_UTF8_INVALID;

                // Is the server expected to respond with 400?
                if (cfg->path_utf8_invalid_unwanted != HTP_UNWANTED_IGNORE) {
                    tx->response_status_expected_number = cfg->path_utf8_invalid_unwanted;
                }

                // Override the state in the UTF-8 decoder because
                // we want to ignore invalid characters
                state = HTP_UTF8_ACCEPT;

                // Copy the invalid bytes into the output stream
                while (charpos <= rpos) {
                    data[wpos++] = data[charpos++];
                }

                // Advance over the consumed byte
                rpos++;

                // Prepare for the next character
                counter = 0;
                charpos = rpos;

                break;

            default:
                // Keep going; the character is not yet formed
                rpos++;
                break;
        }
    }

    // Did the input stream seem like a valid UTF-8 string?
    if ((seen_valid) && (!(tx->flags & HTP_PATH_UTF8_INVALID))) {
        tx->flags |= HTP_PATH_UTF8_VALID;
    }

    // Adjust the length of the string, because
    // we're doing in-place decoding.
    bstr_adjust_len(path, wpos);
}

/**
 * Validate a path that is quite possibly UTF-8 encoded.
 *
 * @param[in] cfg
 * @param[in] tx
 * @param[in] path
 */
void htp_utf8_validate_path(htp_tx_t *tx, bstr *path) {
    unsigned char *data = (unsigned char *) bstr_ptr(path);
    size_t len = bstr_len(path);
    size_t rpos = 0;
    uint32_t codepoint = 0;
    uint32_t state = HTP_UTF8_ACCEPT;
    uint32_t counter = 0;
    uint8_t seen_valid = 0;

    while (rpos < len) {
        counter++;

        switch (htp_utf8_decode_allow_overlong(&state, &codepoint, data[rpos])) {
            case HTP_UTF8_ACCEPT:
                // ASCII character

                if (counter > 1) {
                    // A valid UTF-8 character
                    seen_valid = 1;

                    // Check for overlong characters and set the
                    // flag accordingly
                    switch (counter) {
                        case 2:
                            if (codepoint < 0x80) {
                                tx->flags |= HTP_PATH_UTF8_OVERLONG;
                            }
                            break;
                        case 3:
                            if (codepoint < 0x800) {
                                tx->flags |= HTP_PATH_UTF8_OVERLONG;
                            }
                            break;
                        case 4:
                            if (codepoint < 0x10000) {
                                tx->flags |= HTP_PATH_UTF8_OVERLONG;
                            }
                            break;
                    }
                }

                // Special flag for fullwidth form evasion
                if ((codepoint > 0xfeff) && (codepoint < 0x010000)) {
                    tx->flags |= HTP_PATH_HALF_FULL_RANGE;
                }

                // Advance over the consumed byte
                rpos++;

                // Prepare for the next character
                counter = 0;

                break;

            case HTP_UTF8_REJECT:
                // Invalid UTF-8 character
                tx->flags |= HTP_PATH_UTF8_INVALID;

                // Override the state in the UTF-8 decoder because
                // we want to ignore invalid characters
                state = HTP_UTF8_ACCEPT;

                // Advance over the consumed byte
                rpos++;

                // Prepare for the next character
                counter = 0;

                break;

            default:
                // Keep going; the character is not yet formed
                rpos++;
                break;
        }
    }

    // Did the input stream seem like a valid UTF-8 string?
    if ((seen_valid) && (!(tx->flags & HTP_PATH_UTF8_INVALID))) {
        tx->flags |= HTP_PATH_UTF8_VALID;
    }
}

/**
 * Decode a %u-encoded character, using best-fit mapping as necessary. Path version.
 *
 * @param[in] cfg
 * @param[in] tx
 * @param[in] data
 * @return decoded byte
 */
static int decode_u_encoding_path(htp_cfg_t *cfg, htp_tx_t *tx, unsigned char *data) {
    unsigned int c1 = x2c(data);
    unsigned int c2 = x2c(data + 2);
    int r = cfg->bestfit_replacement_char;

    if (c1 == 0x00) {
        r = c2;
        tx->flags |= HTP_PATH_OVERLONG_U;
    } else {
        // Check for fullwidth form evasion
        if (c1 == 0xff) {
            tx->flags |= HTP_PATH_HALF_FULL_RANGE;
        }

        if (cfg->path_unicode_unwanted != HTP_UNWANTED_IGNORE) {
            tx->response_status_expected_number = cfg->path_unicode_unwanted;
        }

        // Use best-fit mapping
        unsigned char *p = cfg->bestfit_map;

        // TODO Optimize lookup.

        for (;;) {
            // Have we reached the end of the map?
            if ((p[0] == 0) && (p[1] == 0)) {
                break;
            }

            // Have we found the mapping we're looking for?
            if ((p[0] == c1) && (p[1] == c2)) {
                r = p[2];
                break;
            }

            // Move to the next triplet
            p += 3;
        }
    }

    // Check for encoded path separators
    if ((r == '/') || ((cfg->path_backslash_separators) && (r == '\\'))) {
        tx->flags |= HTP_PATH_ENCODED_SEPARATOR;
    }

    return r;
}

/**
 * Decode a %u-encoded character, using best-fit mapping as necessary. Params version.
 *
 * @param[in] cfg
 * @param[in] tx
 * @param[in] data
 * @return decoded byte
 */
static int decode_u_encoding_params(htp_cfg_t *cfg, htp_tx_t *tx, unsigned char *data) {
    unsigned int c1 = x2c(data);
    unsigned int c2 = x2c(data + 2);    

    // Check for overlong usage first.
    if (c1 == 0) {
        tx->flags |= HTP_URLEN_OVERLONG_U;
        
        return c2;
    }

    // Both bytes were used.

    // Detect half-width and full-width range.
    if ((c1 == 0xff) && (c2 <= 0xef)) {
        tx->flags |= HTP_URLEN_HALF_FULL_RANGE;
    }

    // Use best-fit mapping.
    unsigned char *p = cfg->bestfit_map;
    int r = cfg->bestfit_replacement_char;

    // TODO Optimize lookup.

    for (;;) {
        // Have we reached the end of the map?
        if ((p[0] == 0) && (p[1] == 0)) {
            break;
        }

        // Have we found the mapping we're looking for?
        if ((p[0] == c1) && (p[1] == c2)) {
            r = p[2];
            break;
        }

        // Move to the next triplet
        p += 3;
    }

    return r;
}

/**
 * Decode a request path according to the settings in the
 * provided configuration structure.
 *
 * @param[in] cfg
 * @param[in] tx
 * @param[in] path
 */
int htp_decode_path_inplace(htp_cfg_t *cfg, htp_tx_t *tx, bstr *path) {
    unsigned char *data = (unsigned char *) bstr_ptr(path);
    if (data == NULL) {
        return -1;
    }
    size_t len = bstr_len(path);

    // TODO I don't like this function. It's too complex.

    size_t rpos = 0;
    size_t wpos = 0;
    int previous_was_separator = 0;

    while (rpos < len) {
        int c = data[rpos];

        // Decode encoded characters
        if (c == '%') {
            if (rpos + 2 < len) {
                int handled = 0;

                if (cfg->path_u_encoding_decode) {
                    // Check for the %u encoding
                    if ((data[rpos + 1] == 'u') || (data[rpos + 1] == 'U')) {
                        handled = 1;

                        if (cfg->path_u_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                            tx->response_status_expected_number = cfg->path_u_encoding_unwanted;
                        }

                        if (rpos + 5 < len) {
                            if (isxdigit(data[rpos + 2]) && (isxdigit(data[rpos + 3]))
                                    && isxdigit(data[rpos + 4]) && (isxdigit(data[rpos + 5]))) {
                                // Decode a valid %u encoding
                                c = decode_u_encoding_path(cfg, tx, &data[rpos + 2]);
                                rpos += 6;

                                if (c == 0) {
                                    tx->flags |= HTP_PATH_ENCODED_NUL;

                                    if (cfg->path_nul_encoded_unwanted != HTP_UNWANTED_IGNORE) {
                                        tx->response_status_expected_number = cfg->path_nul_encoded_unwanted;
                                    }
                                }
                            } else {
                                // Invalid %u encoding
                                tx->flags |= HTP_PATH_INVALID_ENCODING;

                                if (cfg->path_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                                    tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                                }

                                switch (cfg->path_invalid_encoding_handling) {
                                    case HTP_URL_DECODE_REMOVE_PERCENT:
                                        // Do not place anything in output; eat
                                        // the percent character
                                        rpos++;
                                        continue;
                                        break;
                                    case HTP_URL_DECODE_PRESERVE_PERCENT:
                                        // Leave the percent character in output
                                        rpos++;
                                        break;
                                    case HTP_URL_DECODE_PROCESS_INVALID:
                                        // Decode invalid %u encoding
                                        c = decode_u_encoding_path(cfg, tx, &data[rpos + 2]);
                                        rpos += 6;
                                        break;
                                }
                            }
                        } else {
                            // Invalid %u encoding (not enough data)
                            tx->flags |= HTP_PATH_INVALID_ENCODING;

                            if (cfg->path_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                                tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                            }

                            switch (cfg->path_invalid_encoding_handling) {
                                case HTP_URL_DECODE_REMOVE_PERCENT:
                                    // Do not place anything in output; eat
                                    // the percent character
                                    rpos++;
                                    continue;
                                    break;
                                case HTP_URL_DECODE_PRESERVE_PERCENT:
                                    // Leave the percent character in output
                                    rpos++;
                                    break;
                                case HTP_URL_DECODE_PROCESS_INVALID:
                                    // Cannot decode, because there's not enough data.
                                    break;
                            }
                        }
                    }
                }

                // Handle standard URL encoding
                if (!handled) {
                    if ((isxdigit(data[rpos + 1])) && (isxdigit(data[rpos + 2]))) {
                        c = x2c(&data[rpos + 1]);

                        if (c == 0) {
                            tx->flags |= HTP_PATH_ENCODED_NUL;

                            if (cfg->path_nul_encoded_unwanted != HTP_UNWANTED_IGNORE) {
                                tx->response_status_expected_number = cfg->path_nul_encoded_unwanted;
                            }

                            if (cfg->path_nul_encoded_terminates) {
                                bstr_adjust_len(path, wpos);
                                return 1;
                            }
                        }

                        if ((c == '/') || ((cfg->path_backslash_separators) && (c == '\\'))) {
                            tx->flags |= HTP_PATH_ENCODED_SEPARATOR;

                            if (cfg->path_encoded_separators_unwanted != HTP_UNWANTED_IGNORE) {
                                tx->response_status_expected_number = cfg->path_encoded_separators_unwanted;
                            }

                            if (cfg->path_encoded_separators_decode) {
                                // Decode
                                rpos += 3;
                            } else {
                                // Leave encoded
                                c = '%';
                                rpos++;
                            }
                        } else {
                            // Decode
                            rpos += 3;
                        }
                    } else {
                        // Invalid encoding
                        tx->flags |= HTP_PATH_INVALID_ENCODING;

                        if (cfg->path_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                            tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                        }

                        switch (cfg->path_invalid_encoding_handling) {
                            case HTP_URL_DECODE_REMOVE_PERCENT:
                                // Do not place anything in output; eat
                                // the percent character
                                rpos++;
                                continue;
                                break;
                            case HTP_URL_DECODE_PRESERVE_PERCENT:
                                // Leave the percent character in output
                                rpos++;
                                break;
                            case HTP_URL_DECODE_PROCESS_INVALID:
                                // Decode
                                c = x2c(&data[rpos + 1]);
                                rpos += 3;
                                // Note: What if an invalid encoding decodes into a path
                                //       separator? This is theoretical at the moment, because
                                //       the only platform we know doesn't convert separators is
                                //       Apache, who will also respond with 400 if invalid encoding
                                //       is encountered. Thus no check for a separator here.
                                break;
                            default:
                                // Unknown setting
                                return -1;
                                break;
                        }
                    }
                }
            } else {
                // Invalid %u encoding (not enough data)
                tx->flags |= HTP_PATH_INVALID_ENCODING;

                if (cfg->path_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                    tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                }

                switch (cfg->path_invalid_encoding_handling) {
                    case HTP_URL_DECODE_REMOVE_PERCENT:
                        // Do not place anything in output; eat
                        // the percent character
                        rpos++;
                        continue;
                        break;
                    case HTP_URL_DECODE_PRESERVE_PERCENT:
                        // Leave the percent character in output
                        rpos++;
                        break;
                    case HTP_URL_DECODE_PROCESS_INVALID:
                        // Cannot decode, because there's not enough data.
                        break;
                }
            }
        } else {
            // One non-encoded character

            // Is it a NUL byte?
            if (c == 0) {
                if (cfg->path_nul_raw_unwanted != HTP_UNWANTED_IGNORE) {
                    tx->response_status_expected_number = cfg->path_nul_raw_unwanted;
                }

                if (cfg->path_nul_raw_terminates) {
                    // Terminate path with a raw NUL byte
                    bstr_adjust_len(path, wpos);
                    return 1;
                    break;
                }
            }

            rpos++;
        }

        // Place the character into output

        // Check for control characters
        if (c < 0x20) {
            if (cfg->path_control_chars_unwanted != HTP_UNWANTED_IGNORE) {
                tx->response_status_expected_number = cfg->path_control_chars_unwanted;
            }
        }

        // Convert backslashes to forward slashes, if necessary
        if ((c == '\\') && (cfg->path_backslash_separators)) {
            c = '/';
        }

        // Lowercase characters, if necessary
        if (cfg->path_case_insensitive) {
            c = tolower(c);
        }

        // If we're compressing separators then we need
        // to track if the previous character was a separator
        if (cfg->path_compress_separators) {
            if (c == '/') {
                if (!previous_was_separator) {
                    data[wpos++] = c;
                    previous_was_separator = 1;
                } else {
                    // Do nothing; we don't want
                    // another separator in output
                }
            } else {
                data[wpos++] = c;
                previous_was_separator = 0;
            }
        } else {
            data[wpos++] = c;
        }
    }

    bstr_adjust_len(path, wpos);

    return 1;
}

int htp_decode_urlencoded_inplace(htp_cfg_t *cfg, htp_tx_t *tx, bstr *input) {
    unsigned char *data = bstr_ptr(input);
    size_t len = bstr_len(input);

    // TODO I don't like this function, either.

    size_t rpos = 0;
    size_t wpos = 0;

    while (rpos < len) {
        int c = data[rpos];

        // Decode encoded characters.
        if (c == '%') {
            if (rpos + 2 < len) {
                int handled = 0;

                if (cfg->params_u_encoding_decode) {
                    // Check for the %u encoding.
                    if ((data[rpos + 1] == 'u') || (data[rpos + 1] == 'U')) {
                        handled = 1;

                        if (cfg->params_u_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                            tx->response_status_expected_number = cfg->params_u_encoding_unwanted;
                        }

                        if (rpos + 5 < len) {
                            if (isxdigit(data[rpos + 2]) && (isxdigit(data[rpos + 3]))
                                    && isxdigit(data[rpos + 4]) && (isxdigit(data[rpos + 5]))) {
                                // Decode a valid %u encoding.
                                c = decode_u_encoding_params(cfg, tx, &data[rpos + 2]);
                                rpos += 6;

                                if (c == 0) {
                                    tx->flags |= HTP_URLEN_ENCODED_NUL;

                                    if (cfg->params_nul_encoded_unwanted != HTP_UNWANTED_IGNORE) {
                                        tx->response_status_expected_number = cfg->params_nul_encoded_unwanted;
                                    }
                                }
                            } else {
                                // Invalid %u encoding.
                                tx->flags |= HTP_URLEN_INVALID_ENCODING;

                                if (cfg->params_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                                    tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                                }

                                switch (cfg->params_invalid_encoding_handling) {
                                    case HTP_URL_DECODE_REMOVE_PERCENT:
                                        // Do not place anything in output; consume the %.
                                        rpos++;
                                        continue;
                                        break;
                                    case HTP_URL_DECODE_PRESERVE_PERCENT:
                                        // Leave the % in output.
                                        rpos++;
                                        break;
                                    case HTP_URL_DECODE_PROCESS_INVALID:
                                        // Decode invalid %u encoding.
                                        c = decode_u_encoding_params(cfg, tx, &data[rpos + 2]);
                                        rpos += 6;
                                        break;
                                }
                            }
                        } else {
                            // Invalid %u encoding; not enough data.
                            tx->flags |= HTP_URLEN_INVALID_ENCODING;

                            if (cfg->params_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                                tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                            }

                            switch (cfg->params_invalid_encoding_handling) {
                                case HTP_URL_DECODE_REMOVE_PERCENT:
                                    // Do not place anything in output; consume the %.
                                    rpos++;
                                    continue;
                                    break;
                                case HTP_URL_DECODE_PRESERVE_PERCENT:
                                    // Leave the % in output.
                                    rpos++;
                                    break;
                                case HTP_URL_DECODE_PROCESS_INVALID:
                                    // Cannot decode; not enough data.
                                    break;
                            }
                        }
                    }
                }

                // Handle standard URL encoding.
                if (!handled) {
                    if ((isxdigit(data[rpos + 1])) && (isxdigit(data[rpos + 2]))) {
                        // Decode a %HH encoding.
                        c = x2c(&data[rpos + 1]);
                        rpos += 3;

                        if (c == 0) {
                            tx->flags |= HTP_URLEN_ENCODED_NUL;

                            if (cfg->params_nul_encoded_unwanted != HTP_UNWANTED_IGNORE) {
                                tx->response_status_expected_number = cfg->params_nul_encoded_unwanted;
                            }

                            if (cfg->params_nul_encoded_terminates) {
                                bstr_adjust_len(input, wpos);
                                return 1;
                            }
                        }
                    } else {
                        // Invalid encoding.
                        tx->flags |= HTP_URLEN_INVALID_ENCODING;

                        if (cfg->params_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                            tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                        }

                        switch (cfg->params_invalid_encoding_handling) {
                            case HTP_URL_DECODE_REMOVE_PERCENT:
                                // Do not place anything in output; consume the %.
                                rpos++;
                                continue;
                                break;
                            case HTP_URL_DECODE_PRESERVE_PERCENT:
                                // Leave the % in output.
                                rpos++;
                                break;
                            case HTP_URL_DECODE_PROCESS_INVALID:
                                // Decode.
                                c = x2c(&data[rpos + 1]);
                                rpos += 3;
                                break;
                        }
                    }
                }
            } else {
                // Invalid %u encoding; not enough data.
                tx->flags |= HTP_URLEN_INVALID_ENCODING;

                if (cfg->params_invalid_encoding_unwanted != HTP_UNWANTED_IGNORE) {
                    tx->response_status_expected_number = cfg->path_invalid_encoding_unwanted;
                }

                switch (cfg->params_invalid_encoding_handling) {
                    case HTP_URL_DECODE_REMOVE_PERCENT:
                        // Do not place anything in output; consume the %.
                        rpos++;
                        continue;
                        break;
                    case HTP_URL_DECODE_PRESERVE_PERCENT:
                        // Leave the % in output.
                        rpos++;
                        break;
                    case HTP_URL_DECODE_PROCESS_INVALID:
                        // Cannot decode; not enough data.
                        break;
                }
            }
        } else {
            // One non-encoded character.

            // Is it a NUL byte?
            if (c == 0) {
                if (cfg->params_nul_raw_unwanted != HTP_UNWANTED_IGNORE) {
                    tx->response_status_expected_number = cfg->params_nul_raw_unwanted;
                }

                if (cfg->params_nul_raw_terminates) {
                    // Terminate path with a raw NUL byte.
                    bstr_adjust_len(input, wpos);
                    return 1;
                    break;
                }
            } else if (c == '+') {
                c = 0x20;
            }

            rpos++;
        }

        // Place the character into output.
        data[wpos++] = c;
    }

    bstr_adjust_len(input, wpos);

    return 1;
}

/**
 * Normalize a previously-parsed request URI.
 *
 * @param[in] connp
 * @param[in] incomplete
 * @param[in] normalized
 * @return HTP_OK or HTP_ERROR
 */
int htp_normalize_parsed_uri(htp_connp_t *connp, htp_uri_t *incomplete, htp_uri_t *normalized) {
    // Scheme.
    if (incomplete->scheme != NULL) {
        // Duplicate and convert to lowercase.
        normalized->scheme = bstr_dup_lower(incomplete->scheme);
        if (normalized->scheme == NULL) return HTP_ERROR;
    }

    // Username.
    if (incomplete->username != NULL) {
        normalized->username = bstr_dup(incomplete->username);
        if (normalized->username == NULL) return HTP_ERROR;
        htp_uriencoding_normalize_inplace(normalized->username);
    }

    // Password.
    if (incomplete->password != NULL) {
        normalized->password = bstr_dup(incomplete->password);
        if (normalized->password == NULL) return HTP_ERROR;
        htp_uriencoding_normalize_inplace(normalized->password);
    }

    // Hostname.
    if (incomplete->hostname != NULL) {
        // We know that incomplete->hostname does not contain
        // port information, so no need to check for it here.
        normalized->hostname = bstr_dup(incomplete->hostname);
        if (normalized->hostname == NULL) return HTP_ERROR;
        htp_uriencoding_normalize_inplace(normalized->hostname);
        htp_normalize_hostname_inplace(normalized->hostname);
    }

    if (incomplete->port != NULL) {
        int64_t port_parsed = htp_parse_positive_integer_whitespace(
                bstr_ptr(incomplete->port), bstr_len(incomplete->port), 10);

        if (port_parsed < 0) {
            // Failed to parse the port number.
            normalized->port_number = -1;
            connp->in_tx->flags |= HTP_HOST_INVALID;
        } else if ((port_parsed > 0) && (port_parsed < 65536)) {
            // Valid port number.
            normalized->port_number = (int) port_parsed;
        } else {
            // Port number out of range.
            normalized->port_number = -1;
            connp->in_tx->flags |= HTP_HOST_INVALID;
        }
    }

    // Path.
    if (incomplete->path != NULL) {
        // Make a copy of the path, on which we can work on.
        normalized->path = bstr_dup(incomplete->path);
        if (normalized->path == NULL) return HTP_ERROR;

        // Decode URL-encoded (and %u-encoded) characters, as well as lowercase,
        // compress separators and convert backslashes.
        htp_decode_path_inplace(connp->cfg, connp->in_tx, normalized->path);

        // Handle UTF-8 in path.
        if (connp->cfg->path_utf8_convert) {
            // Decode Unicode characters into a single-byte stream, using best-fit mapping.
            htp_utf8_decode_path_inplace(connp->cfg, connp->in_tx, normalized->path);
        } else {
            // Only validate path as a UTF-8 stream.
            htp_utf8_validate_path(connp->in_tx, normalized->path);
        }

        // RFC normalization.
        htp_normalize_uri_path_inplace(normalized->path);
    }

    // Query string.
    if (incomplete->query != NULL) {
        // We cannot URL-decode the query string here; it needs to be
        // parsed into individual key-value pairs first.
        normalized->query = bstr_dup(incomplete->query);
        if (normalized->query == NULL) return HTP_ERROR;
    }

    // Fragment.
    if (incomplete->fragment != NULL) {
        normalized->fragment = bstr_dup(incomplete->fragment);
        if (normalized->fragment == NULL) return HTP_ERROR;
        htp_uriencoding_normalize_inplace(normalized->fragment);
    }

    return HTP_OK;
}

/**
 * Normalize request hostname. Convert all characters to lowercase and
 * remove trailing dots from the end, if present.
 *
 * @param[in] hostname
 * @return Normalized hostname.
 */
bstr *htp_normalize_hostname_inplace(bstr *hostname) {
    if (hostname == NULL) return NULL;

    bstr_to_lowercase(hostname);

    // Remove dots from the end of the string.    
    while (bstr_char_at_end(hostname, 0) == '.') bstr_chop(hostname);

    return hostname;
}

/**
 * Replace the URI in the structure with the one provided as the parameter
 * to this function (which will typically be supplied in a Host header).
 *
 * @param[in] connp
 * @param[in] parsed_uri
 * @param[in] hostname
 */
void htp_replace_hostname(htp_connp_t *connp, htp_uri_t *parsed_uri, bstr *hostname) {
    if (hostname == NULL) return;

    bstr *new_hostname = NULL;

    int colon = bstr_chr(hostname, ':');
    if (colon == -1) {
        // Hostname alone (no port information)
        new_hostname = bstr_dup(hostname);
        if (new_hostname == NULL) return;
        htp_normalize_hostname_inplace(new_hostname);

        if (parsed_uri->hostname != NULL) bstr_free(parsed_uri->hostname);
        parsed_uri->hostname = new_hostname;
    } else {
        // Hostname and port
        new_hostname = bstr_dup_ex(hostname, 0, colon);
        if (new_hostname == NULL) return;
        // TODO Handle whitespace around hostname
        htp_normalize_hostname_inplace(new_hostname);

        if (parsed_uri->hostname != NULL) bstr_free(parsed_uri->hostname);
        parsed_uri->hostname = new_hostname;
        parsed_uri->port_number = 0;

        // Port
        int port = htp_parse_positive_integer_whitespace((unsigned char *) bstr_ptr(hostname) + colon + 1,
                bstr_len(hostname) - colon - 1, 10);
        if (port < 0) {
            // Failed to parse port
            htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0, "Invalid server port information in request");
        } else if ((port > 0) && (port < 65536)) {
            // Valid port
            if ((connp->conn->server_port != 0) && (port != connp->conn->server_port)) {
                // Port was specified in connection and is different from the TCP port
                htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0, "Request server port=%d number differs from the actual TCP port=%d", port, connp->conn->server_port);
            } else {
                parsed_uri->port_number = port;
            }
        }
    }
}

/**
 * Is URI character reserved?
 *
 * @param[in] c
 * @return 1 if it is, 0 if it isn't
 */
int htp_is_uri_unreserved(unsigned char c) {
    if (((c >= 0x41) && (c <= 0x5a)) ||
            ((c >= 0x61) && (c <= 0x7a)) ||
            ((c >= 0x30) && (c <= 0x39)) ||
            (c == 0x2d) || (c == 0x2e) ||
            (c == 0x5f) || (c == 0x7e)) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * Decode a URL-encoded string, leaving the reserved
 * characters and invalid encodings alone.
 *
 * @param[in] s
 */
void htp_uriencoding_normalize_inplace(bstr *s) {
    if (s == NULL) return;

    unsigned char *data = (unsigned char *) bstr_ptr(s);
    size_t len = bstr_len(s);

    size_t rpos = 0;
    size_t wpos = 0;

    while (rpos < len) {
        if (data[rpos] == '%') {
            if (rpos + 2 < len) {
                if (isxdigit(data[rpos + 1]) && (isxdigit(data[rpos + 2]))) {
                    unsigned char c = x2c(&data[rpos + 1]);

                    if (htp_is_uri_unreserved(c)) {
                        // Leave reserved characters encoded, but convert
                        // the hexadecimal digits to uppercase
                        data[wpos++] = data[rpos++];
                        data[wpos++] = toupper(data[rpos++]);
                        data[wpos++] = toupper(data[rpos++]);
                    } else {
                        // Decode unreserved character
                        data[wpos++] = c;
                        rpos += 3;
                    }
                } else {
                    // Invalid URL encoding: invalid hex digits

                    // Copy over what's there
                    data[wpos++] = data[rpos++];
                    data[wpos++] = toupper(data[rpos++]);
                    data[wpos++] = toupper(data[rpos++]);
                }
            } else {
                // Invalid URL encoding: string too short

                // Copy over what's there
                data[wpos++] = data[rpos++];
                while (rpos < len) {
                    data[wpos++] = toupper(data[rpos++]);
                }
            }
        } else {
            data[wpos++] = data[rpos++];
        }
    }

    bstr_adjust_len(s, wpos);
}

/**
 * Normalize URL path. This function implements the remove dot segments algorithm
 * specified in RFC 3986, section 5.2.4.
 *
 * @param[in] s
 */
void htp_normalize_uri_path_inplace(bstr *s) {
    if (s == NULL) return;

    unsigned char *data = bstr_ptr(s);
    size_t len = bstr_len(s);

    size_t rpos = 0;
    size_t wpos = 0;

    int c = -1;
    while (rpos < len) {
        if (c == -1) {
            c = data[rpos++];
        }

        // A. If the input buffer begins with a prefix of "../" or "./",
        //    then remove that prefix from the input buffer; otherwise,
        if (c == '.') {
            if ((rpos + 1 < len) && (data[rpos] == '.') && (data[rpos + 1] == '/')) {
                c = -1;
                rpos += 2;
                continue;
            } else if ((rpos < len) && (data[rpos + 1] == '/')) {
                c = -1;
                rpos += 2;
                continue;
            }
        }

        if (c == '/') {
            // B. if the input buffer begins with a prefix of "/./" or "/.",
            //    where "." is a complete path segment, then replace that
            //    prefix with "/" in the input buffer; otherwise,
            if ((rpos + 1 < len) && (data[rpos] == '.') && (data[rpos + 1] == '/')) {
                c = '/';
                rpos += 2;
                continue;
            } else if ((rpos + 1 == len) && (data[rpos] == '.')) {
                c = '/';
                rpos += 1;
                continue;
            }

            // C. if the input buffer begins with a prefix of "/../" or "/..",
            //    where ".." is a complete path segment, then replace that
            //    prefix with "/" in the input buffer and remove the last
            //    segment and its preceding "/" (if any) from the output
            //    buffer; otherwise,
            if ((rpos + 2 < len) && (data[rpos] == '.') && (data[rpos + 1] == '.') && (data[rpos + 2] == '/')) {
                c = '/';
                rpos += 3;

                // Remove the last segment
                while ((wpos > 0) && (data[wpos - 1] != '/')) wpos--;
                if (wpos > 0) wpos--;
                continue;
            } else if ((rpos + 2 == len) && (data[rpos] == '.') && (data[rpos + 1] == '.')) {
                c = '/';
                rpos += 2;

                // Remove the last segment
                while ((wpos > 0) && (data[wpos - 1] != '/')) wpos--;
                if (wpos > 0) wpos--;
                continue;
            }
        }

        // D.  if the input buffer consists only of "." or "..", then remove
        // that from the input buffer; otherwise,
        if ((c == '.') && (rpos == len)) {
            rpos++;
            continue;
        }

        if ((c == '.') && (rpos + 1 == len) && (data[rpos] == '.')) {
            rpos += 2;
            continue;
        }

        // E.  move the first path segment in the input buffer to the end of
        // the output buffer, including the initial "/" character (if
        // any) and any subsequent characters up to, but not including,
        // the next "/" character or the end of the input buffer.
        data[wpos++] = c;

        while ((rpos < len) && (data[rpos] != '/')) {
            // data[wpos++] = data[rpos++];
            int c2 = data[rpos++];
            data[wpos++] = c2;
        }

        c = -1;
    }

    bstr_adjust_len(s, wpos);
}

/**
 *
 */
void fprint_bstr(FILE *stream, const char *name, bstr *b) {
    if (b == NULL) {
        fprint_raw_data_ex(stream, name, (unsigned char *) "(null)", 0, 6);
        return;
    }

    fprint_raw_data_ex(stream, name, (unsigned char *) bstr_ptr(b), 0, bstr_len(b));
}

/**
 *
 */
void fprint_raw_data(FILE *stream, const char *name, const void *data, size_t len) {
    fprint_raw_data_ex(stream, name, data, 0, len);
}

/**
 *
 */
void fprint_raw_data_ex(FILE *stream, const char *name, const void *_data, size_t offset, size_t printlen) {
    const unsigned char *data = (const unsigned char *) _data;
    char buf[160];
    size_t len = offset + printlen;

    fprintf(stream, "\n%s: ptr %p offset %" PRIu64 " len %" PRIu64"\n", name, data, (uint64_t) offset, (uint64_t) len);

    while (offset < len) {
        size_t i;

        sprintf(buf, "%08" PRIx64, (uint64_t) offset);
        strcat(buf + strlen(buf), "  ");

        i = 0;
        while (i < 8) {
            if (offset + i < len) {
                sprintf(buf + strlen(buf), "%02x ", data[offset + i]);
            } else {
                strcat(buf + strlen(buf), "   ");
            }

            i++;
        }

        strcat(buf + strlen(buf), " ");

        i = 8;
        while (i < 16) {
            if (offset + i < len) {
                sprintf(buf + strlen(buf), "%02x ", data[offset + i]);
            } else {
                strcat(buf + strlen(buf), "   ");
            }

            i++;
        }

        strcat(buf + strlen(buf), " |");

        i = 0;
        char *p = buf + strlen(buf);
        while ((offset + i < len) && (i < 16)) {
            int c = data[offset + i];

            if (isprint(c)) {
                *p++ = c;
            } else {
                *p++ = '.';
            }

            i++;
        }

        *p++ = '|';
        *p++ = '\n';
        *p = '\0';

        fprintf(stream, "%s", buf);
        offset += 16;
    }

    fprintf(stream, "\n");
}

/**
 *
 */
char *htp_connp_in_state_as_string(htp_connp_t *connp) {
    if (connp == NULL) return "NULL";

    if (connp->in_state == htp_connp_REQ_IDLE) return "REQ_IDLE";
    if (connp->in_state == htp_connp_REQ_LINE) return "REQ_FIRST_LINE";
    if (connp->in_state == htp_connp_REQ_PROTOCOL) return "REQ_PROTOCOL";
    if (connp->in_state == htp_connp_REQ_HEADERS) return "REQ_HEADERS";
    if (connp->in_state == htp_connp_REQ_BODY_DETERMINE) return "REQ_BODY_DETERMINE";
    if (connp->in_state == htp_connp_REQ_BODY_IDENTITY) return "REQ_BODY_IDENTITY";
    if (connp->in_state == htp_connp_REQ_BODY_CHUNKED_LENGTH) return "REQ_BODY_CHUNKED_LENGTH";
    if (connp->in_state == htp_connp_REQ_BODY_CHUNKED_DATA) return "REQ_BODY_CHUNKED_DATA";
    if (connp->in_state == htp_connp_REQ_BODY_CHUNKED_DATA_END) return "REQ_BODY_CHUNKED_DATA_END";

    if (connp->in_state == htp_connp_REQ_CONNECT_CHECK) return "htp_connp_REQ_CONNECT_CHECK";
    if (connp->in_state == htp_connp_REQ_CONNECT_WAIT_RESPONSE) return "htp_connp_REQ_CONNECT_WAIT_RESPONSE";

    return "UNKNOWN";
}

/**
 *
 */
char *htp_connp_out_state_as_string(htp_connp_t *connp) {
    if (connp == NULL) return "NULL";

    if (connp->out_state == htp_connp_RES_IDLE) return "RES_IDLE";
    if (connp->out_state == htp_connp_RES_LINE) return "RES_LINE";
    if (connp->out_state == htp_connp_RES_HEADERS) return "RES_HEADERS";
    if (connp->out_state == htp_connp_RES_BODY_DETERMINE) return "RES_BODY_DETERMINE";
    if (connp->out_state == htp_connp_RES_BODY_IDENTITY) return "RES_BODY_IDENTITY";
    if (connp->out_state == htp_connp_RES_BODY_CHUNKED_LENGTH) return "RES_BODY_CHUNKED_LENGTH";
    if (connp->out_state == htp_connp_RES_BODY_CHUNKED_DATA) return "RES_BODY_CHUNKED_DATA";
    if (connp->out_state == htp_connp_RES_BODY_CHUNKED_DATA_END) return "RES_BODY_CHUNKED_DATA_END";

    return "UNKNOWN";
}

/**
 *
 */
char *htp_tx_progress_as_string(htp_tx_t *tx) {
    if (tx == NULL) return "NULL";

    switch (tx->progress) {
        case HTP_REQUEST_START:
            return "NEW";
        case HTP_REQUEST_LINE:
            return "REQ_LINE";
        case HTP_REQUEST_HEADERS:
            return "REQ_HEADERS";
        case HTP_REQUEST_BODY:
            return "REQ_BODY";
        case HTP_REQUEST_TRAILER:
            return "REQ_TRAILER";
        case HTP_REQUEST_COMPLETE:
            return "WAIT";
        case HTP_RESPONSE_LINE:
            return "RES_LINE";
        case HTP_RESPONSE_HEADERS:
            return "RES_HEADERS";
        case HTP_RESPONSE_BODY:
            return "RES_BODY";
        case HTP_RESPONSE_TRAILER:
            return "RES_TRAILER";
        case HTP_RESPONSE_COMPLETE:
            return "DONE";
    }

    return "UNKOWN";
}

/**
 *
 */
bstr *htp_unparse_uri_noencode(htp_uri_t *uri) {
    if (uri == NULL) {
        return NULL;
    }

    // On the first pass determine the length of the final string
    size_t len = 0;

    if (uri->scheme != NULL) {
        len += bstr_len(uri->scheme);
        len += 3; // "://"
    }

    if ((uri->username != NULL) || (uri->password != NULL)) {
        if (uri->username != NULL) {
            len += bstr_len(uri->username);
        }

        len += 1; // ":"

        if (uri->password != NULL) {
            len += bstr_len(uri->password);
        }

        len += 1; // "@"
    }

    if (uri->hostname != NULL) {
        len += bstr_len(uri->hostname);
    }

    if (uri->port != NULL) {
        len += 1; // ":"
        len += bstr_len(uri->port);
    }

    if (uri->path != NULL) {
        len += bstr_len(uri->path);
    }

    if (uri->query != NULL) {
        len += 1; // "?"
        len += bstr_len(uri->query);
    }

    if (uri->fragment != NULL) {
        len += 1; // "#"
        len += bstr_len(uri->fragment);
    }

    // On the second pass construct the string
    bstr *r = bstr_alloc(len);
    if (r == NULL) {
        return NULL;
    }

    if (uri->scheme != NULL) {
        bstr_add_noex(r, uri->scheme);
        bstr_add_c_noex(r, "://");
    }

    if ((uri->username != NULL) || (uri->password != NULL)) {
        if (uri->username != NULL) {
            bstr_add_noex(r, uri->username);
        }

        bstr_add_c(r, ":");

        if (uri->password != NULL) {
            bstr_add_noex(r, uri->password);
        }

        bstr_add_c_noex(r, "@");
    }

    if (uri->hostname != NULL) {
        bstr_add_noex(r, uri->hostname);
    }

    if (uri->port != NULL) {
        bstr_add_c(r, ":");
        bstr_add_noex(r, uri->port);
    }

    if (uri->path != NULL) {
        bstr_add_noex(r, uri->path);
    }

    if (uri->query != NULL) {
        bstr *query = bstr_dup(uri->query);
        htp_uriencoding_normalize_inplace(query);
        bstr_add_c_noex(r, "?");
        bstr_add_noex(r, query);
        bstr_free(query);
    }

    if (uri->fragment != NULL) {
        bstr_add_c_noex(r, "#");
        bstr_add_noex(r, uri->fragment);
    }

    return r;
}

/**
 * Determine if the information provided on the response line
 * is good enough. Browsers are lax when it comes to response
 * line parsing. In most cases they will only look for the
 * words "http" at the beginning.
 *
 * @param[in] tx
 * @return 1 for good enough or 0 for not good enough
 */
int htp_treat_response_line_as_body(htp_tx_t *tx) {
    // Browser behavior:
    //      Firefox 3.5.x: (?i)^\s*http
    //      IE: (?i)^\s*http\s*/
    //      Safari: ^HTTP/\d+\.\d+\s+\d{3}

    if (tx->response_protocol == NULL) return 1;
    if (bstr_len(tx->response_protocol) < 4) return 1;

    unsigned char *data = bstr_ptr(tx->response_protocol);

    if ((data[0] != 'H') && (data[0] != 'h')) return 1;
    if ((data[1] != 'T') && (data[1] != 't')) return 1;
    if ((data[2] != 'T') && (data[2] != 't')) return 1;
    if ((data[3] != 'P') && (data[3] != 'p')) return 1;

    return 0;
}

/**
 * Construct a bstr that contains the raw request headers.
 *
 * @param[in] tx
 * @return
 */
bstr *htp_tx_generate_request_headers_raw(htp_tx_t *tx) {
    bstr *request_headers_raw = NULL;
    size_t i, len = 0;

    for (i = 0; i < htp_list_size(tx->request_header_lines); i++) {
        htp_header_line_t *hl = htp_list_get(tx->request_header_lines, i);
        len += bstr_len(hl->line);
    }

    if (tx->request_headers_sep != NULL) {
        len += bstr_len(tx->request_headers_sep);
    }

    request_headers_raw = bstr_alloc(len);
    if (request_headers_raw == NULL) {
        htp_log(tx->connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0, "Failed to allocate bstring of %d bytes", len);
        return NULL;
    }

    for (i = 0; i < htp_list_size(tx->request_header_lines); i++) {
        htp_header_line_t *hl = htp_list_get(tx->request_header_lines, i);
        bstr_add_noex(request_headers_raw, hl->line);
    }

    if (tx->request_headers_sep != NULL) {
        bstr_add_noex(request_headers_raw, tx->request_headers_sep);
    }

    return request_headers_raw;
}

bstr *htp_tx_get_request_headers_raw(htp_tx_t *tx) {
    // Check that we are not called too early
    if (tx->progress < HTP_REQUEST_HEADERS) return NULL;

    if (tx->request_headers_raw == NULL) {
        tx->request_headers_raw = htp_tx_generate_request_headers_raw(tx);
        tx->request_headers_raw_lines = htp_list_size(tx->request_header_lines);
    } else {
        // Check that the buffer we have is not obsolete
        if (tx->request_headers_raw_lines < htp_list_size(tx->request_header_lines)) {
            // Rebuild raw buffer
            bstr_free(tx->request_headers_raw);
            tx->request_headers_raw = htp_tx_generate_request_headers_raw(tx);
            tx->request_headers_raw_lines = htp_list_size(tx->request_header_lines);
        }
    }

    return tx->request_headers_raw;
}

/**
 * Construct a bstr that contains the raw response headers.
 *
 * @param[in] tx
 * @return
 */
bstr *htp_tx_generate_response_headers_raw(htp_tx_t *tx) {
    bstr *response_headers_raw = NULL;
    size_t i, len = 0;

    for (i = 0; i < htp_list_size(tx->response_header_lines); i++) {
        htp_header_line_t *hl = htp_list_get(tx->response_header_lines, i);
        len += bstr_len(hl->line);
    }

    len += bstr_len(tx->response_headers_sep);

    response_headers_raw = bstr_alloc(len);
    if (response_headers_raw == NULL) {
        htp_log(tx->connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0, "Failed to allocate bstring of %d bytes", len);
        return NULL;
    }

    for (i = 0; i < htp_list_size(tx->response_header_lines); i++) {
        htp_header_line_t *hl = htp_list_get(tx->response_header_lines, i);
        bstr_add_noex(response_headers_raw, hl->line);
    }

    bstr_add_noex(response_headers_raw, tx->response_headers_sep);

    return response_headers_raw;
}

bstr *htp_tx_get_response_headers_raw(htp_tx_t *tx) {
    // Check that we are not called too early
    if (tx->progress < HTP_RESPONSE_HEADERS) return NULL;

    if (tx->response_headers_raw == NULL) {
        tx->response_headers_raw = htp_tx_generate_response_headers_raw(tx);
        tx->response_headers_raw_lines = htp_list_size(tx->response_header_lines);
    } else {
        // Check that the buffer we have is not obsolete
        if (tx->response_headers_raw_lines < htp_list_size(tx->response_header_lines)) {
            // Rebuild raw buffer
            bstr_free(tx->response_headers_raw);
            tx->response_headers_raw = htp_tx_generate_response_headers_raw(tx);
            tx->response_headers_raw_lines = htp_list_size(tx->response_header_lines);
        }
    }

    return tx->response_headers_raw;
}

/**
 * Run the REQUEST_BODY_DATA hook.
 *
 * @param[in] connp
 * @param[in] d
 */
int htp_req_run_hook_body_data(htp_connp_t *connp, htp_tx_data_t *d) {
    // Do not invoke callbacks with an empty data chunk
    if ((d->data != NULL) && (d->len == 0)) {
        return HTP_OK;
    }

    // Run transaction hooks first
    int rc = htp_hook_run_all(connp->in_tx->hook_request_body_data, d);
    if (rc != HTP_OK) return rc;

    // Run configuration hooks second
    rc = htp_hook_run_all(connp->cfg->hook_request_body_data, d);
    if (rc != HTP_OK) return rc;

    // On PUT requests, treat request body as file
    if (connp->put_file != NULL) {
        htp_file_data_t file_data;

        file_data.data = d->data;
        file_data.len = d->len;
        file_data.file = connp->put_file;
        file_data.file->len += d->len;

        htp_hook_run_all(connp->cfg->hook_request_file_data, &file_data);
        if (rc != HTP_OK) return rc;
    }

    return rc;
}

/**
 * Run the RESPONSE_BODY_DATA hook.
 *
 * @param[in] connp
 * @param[in] d
 */
int htp_res_run_hook_body_data(htp_connp_t *connp, htp_tx_data_t *d) {
    // Do not invoke callbacks with an empty data chunk
    if ((d->data != NULL) && (d->len == 0)) {
        return HTP_OK;
    }

    // Run transaction hooks first
    int rc = htp_hook_run_all(connp->out_tx->hook_response_body_data, d);
    if (rc != HTP_OK) return rc;

    // Run configuration hooks second
    rc = htp_hook_run_all(connp->cfg->hook_response_body_data, d);
    if (rc != HTP_OK) return rc;

    return HTP_OK;
}

bstr *htp_extract_quoted_string_as_bstr(unsigned char *data, size_t len, size_t *endoffset) {
    size_t pos = 0;
    size_t escaped_chars = 0;

    // Check the first character
    if (data[pos] != '"') return NULL;

    // Step over double quote
    pos++;
    if (pos == len) return NULL;

    // Calculate length
    while (pos < len) {
        if (data[pos] == '\\') {
            if (pos + 1 < len) {
                escaped_chars++;
                pos += 2;
                continue;
            }
        } else if (data[pos] == '"') {
            break;
        }

        pos++;
    }

    if (pos == len) {
        return NULL;
    }

    // Copy the data and unescape the escaped characters
    size_t outlen = pos - 1 - escaped_chars;
    bstr *result = bstr_alloc(outlen);
    if (result == NULL) return NULL;
    unsigned char *outptr = bstr_ptr(result);
    size_t outpos = 0;

    pos = 1;
    while ((pos < len) && (outpos < outlen)) {
        if (data[pos] == '\\') {
            if (pos + 1 < len) {
                outptr[outpos++] = data[pos + 1];
                pos += 2;
                continue;
            }
        } else if (data[pos] == '"') {
            break;
        }

        outptr[outpos++] = data[pos++];
    }

    bstr_adjust_len(result, outlen);

    if (endoffset != NULL) {
        *endoffset = pos;
    }

    return result;
}

htp_status_t htp_parse_ct_header(bstr *header, bstr **ct) {
    if ((header == NULL) || (ct == NULL)) return HTP_ERROR;

    unsigned char *data = bstr_ptr(header);
    size_t len = bstr_len(header);

    // The assumption here is that the header value we receive
    // here has been left-trimmed, which means the starting position
    // is on the mediate type. On some platform that may not be the
    // case, and we may need to do the left-trim ourselves.

    // Find the end of the MIME type, using the same approach PHP 5.4.3 uses.
    size_t pos = 0;
    while ((pos < len) && (data[pos] != ';') && (data[pos] != ',') && (data[pos] != ' ')) pos++;

    *ct = bstr_dup_ex(header, 0, pos);
    if (*ct == NULL) return HTP_ERROR;

    bstr_to_lowercase(*ct);

    return HTP_OK;
}
