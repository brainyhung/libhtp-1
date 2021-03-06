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

#include <stdlib.h>

#include "htp.h"
#include "htp_private.h"
#include "htp_transaction.h"

/**
 * Performs check for a CONNECT transaction to decide whether inbound
 * parsing needs to be suspended.
 *
 * @param[in] connp
 * @return HTP_OK if the request does not use CONNECT, HTP_DATA_OTHER if
 *          inbound parsing needs to be suspended until we hear from the
 *          other side
 */
htp_status_t htp_connp_REQ_CONNECT_CHECK(htp_connp_t *connp) {
    // If the request uses the CONNECT method, then there will
    // not be a request body, but first we need to wait to see the
    // response in order to determine if the tunneling request
    // was a success.
    if (connp->in_tx->request_method_number == HTP_M_CONNECT) {
        connp->in_state = htp_connp_REQ_CONNECT_WAIT_RESPONSE;
        connp->in_status = HTP_STREAM_DATA_OTHER;
        connp->in_tx->progress = HTP_REQUEST_COMPLETE;

        return HTP_DATA_OTHER;
    }

    // Continue to the next step to determine 
    // the presence of request body
    connp->in_state = htp_connp_REQ_BODY_DETERMINE;

    return HTP_OK;
}

/**
 * Determines whether inbound parsing, which was suspended after
 * encountering a CONNECT transaction, can proceed (after receiving
 * the response).
 *
 * @param[in] connp
 * @return HTP_OK if the parser can resume parsing, HTP_DATA_OTHER if
 *         it needs to continue waiting.
 */
htp_status_t htp_connp_REQ_CONNECT_WAIT_RESPONSE(htp_connp_t *connp) {
    // Check that we saw the response line of the current
    // inbound transaction.
    if (connp->in_tx->progress <= HTP_RESPONSE_LINE) {
        return HTP_DATA_OTHER;
    }

    // A 2xx response means a tunnel was established. Anything
    // else means we continue to follow the HTTP stream.
    if ((connp->in_tx->response_status_number >= 200) && (connp->in_tx->response_status_number <= 299)) {
        // TODO Check that the server did not accept a connection to itself.

        // The requested tunnel was established: we are going
        // to ignore the remaining data on this stream
        connp->in_status = HTP_STREAM_TUNNEL;
        connp->in_state = htp_connp_REQ_FINALIZE;
    } else {
        // No tunnel; continue to the next transaction
        connp->in_state = htp_connp_REQ_FINALIZE;
    }

    return HTP_OK;
}

/**
 * Consumes bytes until the end of the current line.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_BODY_CHUNKED_DATA_END(htp_connp_t *connp) {
    // TODO We shouldn't really see anything apart from CR and LF,
    //      so we should warn about anything else.

    for (;;) {
        IN_NEXT_BYTE_OR_RETURN(connp);

        connp->in_tx->request_message_len++;

        if (connp->in_next_byte == LF) {
            connp->in_state = htp_connp_REQ_BODY_CHUNKED_LENGTH;
            return HTP_OK;
        }
    }

    return HTP_ERROR;
}

/**
 * Processes a chunk of data.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_BODY_CHUNKED_DATA(htp_connp_t *connp) {
    unsigned char *data = connp->in_current_data + connp->in_current_offset;
    size_t len = 0;

    for (;;) {
        IN_NEXT_BYTE(connp);

        if (connp->in_next_byte == -1) {
            int rc = htp_tx_req_process_body_data(connp->in_tx, data, len);
            if (rc != HTP_OK) return rc;

            // Ask for more data
            return HTP_DATA;
        } else {
            connp->in_tx->request_message_len++;
            connp->in_chunked_length--;
            len++;

            if (connp->in_chunked_length == 0) {
                // End of data chunk
                int rc = htp_tx_req_process_body_data(connp->in_tx, data, len);
                if (rc != HTP_OK) return rc;

                connp->in_state = htp_connp_REQ_BODY_CHUNKED_DATA_END;

                return HTP_OK;
            }
        }
    }

    return HTP_ERROR;
}

/**
 * Extracts chunk length.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_BODY_CHUNKED_LENGTH(htp_connp_t *connp) {
    for (;;) {
        IN_COPY_BYTE_OR_RETURN(connp);

        connp->in_tx->request_message_len++;

        // Have we reached the end of the line?
        if (connp->in_next_byte == LF) {
            htp_chomp(connp->in_line, &connp->in_line_len);

            // Extract chunk length.
            connp->in_chunked_length = htp_parse_chunked_length(connp->in_line, connp->in_line_len);

            // Cleanup for the next line.
            connp->in_line_len = 0;

            // Handle chunk length
            if (connp->in_chunked_length > 0) {
                // More data available.
                // TODO Add a check (flag) for excessive chunk length.
                connp->in_state = htp_connp_REQ_BODY_CHUNKED_DATA;
            } else if (connp->in_chunked_length == 0) {
                // End of data
                connp->in_state = htp_connp_REQ_HEADERS;
                connp->in_tx->progress = HTP_REQUEST_TRAILER;
            } else {
                // Invalid chunk length.
                htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0,
                        "Request chunk encoding: Invalid chunk length");
                return HTP_ERROR;
            }

            return HTP_OK;
        }
    }

    return HTP_ERROR;
}

/**
 * Processes identity request body.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_BODY_IDENTITY(htp_connp_t *connp) {
    unsigned char *data = connp->in_current_data + connp->in_current_offset;
    size_t len = 0;

    for (;;) {
        IN_NEXT_BYTE(connp);

        if (connp->in_next_byte == -1) {
            // End of chunk

            int rc = htp_tx_req_process_body_data(connp->in_tx, data, len);
            if (rc != HTP_OK) return rc;

            // Ask for more data
            return HTP_DATA;
        } else {
            connp->in_tx->request_message_len++;
            connp->in_body_data_left--;
            len++;

            if (connp->in_body_data_left == 0) {
                // End of body

                int rc = htp_tx_req_process_body_data(connp->in_tx, data, len);
                if (rc != HTP_OK) return rc;

                // Move to finalize the request
                connp->in_state = htp_connp_REQ_FINALIZE;

                return HTP_OK;
            }
        }
    }

    return HTP_ERROR;
}

/**
 * Determines presence (and encoding) of a request body.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_BODY_DETERMINE(htp_connp_t *connp) {
    // Determine the next state based on the presence of the request
    // body, and the coding used.
    switch (connp->in_tx->request_transfer_coding) {

        case HTP_CODING_CHUNKED:
            connp->in_state = htp_connp_REQ_BODY_CHUNKED_LENGTH;
            connp->in_tx->progress = HTP_REQUEST_BODY;
            break;

        case HTP_CODING_IDENTITY:
            connp->in_content_length = connp->in_tx->request_content_length;
            connp->in_body_data_left = connp->in_content_length;

            if (connp->in_content_length != 0) {
                connp->in_state = htp_connp_REQ_BODY_IDENTITY;
                connp->in_tx->progress = HTP_REQUEST_BODY;
            } else {
                connp->in_tx->connp->in_state = htp_connp_REQ_FINALIZE;
            }
            break;
        case HTP_CODING_NO_BODY:
            // This request does not have a body, which
            // means that we're done with it
            connp->in_state = htp_connp_REQ_FINALIZE;
            break;

        default:
            // Should not be here
            return HTP_ERROR;
            break;
    }

    return HTP_OK;
}

/**
 * Parses request headers.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_HEADERS(htp_connp_t *connp) {
    for (;;) {
        IN_COPY_BYTE_OR_RETURN(connp);

        // Allocate structure to hold one header line
        if (connp->in_header_line == NULL) {
            connp->in_header_line = calloc(1, sizeof (htp_header_line_t));
            if (connp->in_header_line == NULL) return HTP_ERROR;
            connp->in_header_line->first_nul_offset = -1;
        }

        // Keep track of NUL bytes
        if (connp->in_next_byte == 0) {
            // Store the offset of the first NUL
            if (connp->in_header_line->has_nulls == 0) {
                connp->in_header_line->first_nul_offset = connp->in_line_len;
            }

            // Remember how many NULs there were
            connp->in_header_line->flags |= HTP_FIELD_RAW_NUL;
            connp->in_header_line->has_nulls++;
        }

        // Have we reached the end of the line?
        if (connp->in_next_byte == LF) {
            #ifdef HTP_DEBUG
            fprint_raw_data(stderr, __FUNCTION__, connp->in_line, connp->in_line_len);
            #endif

            // Should we terminate headers?
            if (htp_connp_is_line_terminator(connp, connp->in_line, connp->in_line_len)) {
                if (connp->in_tx->request_headers_sep != NULL) {
                    bstr_free(connp->in_tx->request_headers_sep);
                    connp->in_tx->request_headers_sep = NULL;
                }

                // Terminator line
                connp->in_tx->request_headers_sep = bstr_dup_mem(connp->in_line, connp->in_line_len);
                if (connp->in_tx->request_headers_sep == NULL) {
                    return HTP_ERROR;
                }

                // Parse previous header, if any
                if (connp->in_header_line_index != -1) {
                    if (connp->cfg->process_request_header(connp) != HTP_OK) {
                        // Note: downstream responsible for error logging
                        return HTP_ERROR;
                    }

                    // Reset index
                    connp->in_header_line_index = -1;
                }

                // Cleanup
                free(connp->in_header_line);
                connp->in_line_len = 0;
                connp->in_header_line = NULL;

                // We've seen all request headers
                return htp_tx_state_request_headers(connp->in_tx);
            }

            // Prepare line for consumption
            int chomp_result = htp_chomp(connp->in_line, &connp->in_line_len);

            // Check for header folding
            if (htp_connp_is_line_folded(connp->in_line, connp->in_line_len) == 0) {
                // New header line

                // Parse previous header, if any
                if (connp->in_header_line_index != -1) {
                    if (connp->cfg->process_request_header(connp) != HTP_OK) {
                        // Note: downstream responsible for error logging
                        return HTP_ERROR;
                    }

                    // Reset index
                    connp->in_header_line_index = -1;
                }

                // Remember the index of the fist header line
                connp->in_header_line_index = connp->in_header_line_counter;
            } else {
                // Folding; check that there's a previous header line to add to
                if (connp->in_header_line_index == -1) {
                    if (!(connp->in_tx->flags & HTP_INVALID_FOLDING)) {
                        connp->in_tx->flags |= HTP_INVALID_FOLDING;
                        htp_log(connp, HTP_LOG_MARK, HTP_LOG_WARNING, 0,
                                "Invalid request field folding");
                    }
                }
            }

            // Add the raw header line to the list
            connp->in_header_line->line = bstr_dup_mem(connp->in_line, connp->in_line_len + chomp_result);
            if (connp->in_header_line->line == NULL) {
                return HTP_ERROR;
            }

            htp_list_add(connp->in_tx->request_header_lines, connp->in_header_line);
            connp->in_header_line = NULL;

            // Cleanup for the next line
            connp->in_line_len = 0;
            if (connp->in_header_line_index == -1) {
                connp->in_header_line_index = connp->in_header_line_counter;
            }

            connp->in_header_line_counter++;
        }
    }

    return HTP_ERROR;
}

/**
 * Determines request protocol.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_PROTOCOL(htp_connp_t *connp) {
    // Is this a short-style HTTP/0.9 request? If it is,
    // we will not want to parse request headers.
    if (connp->in_tx->is_protocol_0_9 == 0) {
        // Switch to request header parsing.
        connp->in_state = htp_connp_REQ_HEADERS;
        connp->in_tx->progress = HTP_REQUEST_HEADERS;
    } else {
        // We're done with this request.
        connp->in_state = htp_connp_REQ_FINALIZE;
    }

    return HTP_OK;
}

/**
 * Parses request line.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_LINE(htp_connp_t *connp) {
    for (;;) {
        // Get one byte
        IN_COPY_BYTE_OR_RETURN(connp);

        // Keep track of NUL bytes
        if (connp->in_next_byte == 0) {
            // Remember how many NULs there were
            connp->in_tx->request_line_nul++;

            // Store the offset of the first NUL byte
            if (connp->in_tx->request_line_nul_offset == -1) {
                connp->in_tx->request_line_nul_offset = connp->in_line_len;
            }
        }

        // Have we reached the end of the line?
        if (connp->in_next_byte == LF) {
            #ifdef HTP_DEBUG
            fprint_raw_data(stderr, __FUNCTION__, connp->in_line, connp->in_line_len);
            #endif

            // Is this a line that should be ignored?
            if (htp_connp_is_line_ignorable(connp, connp->in_line, connp->in_line_len)) {
                // We have an empty/whitespace line, which we'll note, ignore and move on
                connp->in_tx->request_ignored_lines++;

                // TODO How many empty lines are we willing to accept?

                // Start again
                connp->in_line_len = 0;

                return HTP_OK;
            }

            // Process request line

            connp->in_tx->request_line_raw = bstr_dup_mem(connp->in_line, connp->in_line_len);
            if (connp->in_tx->request_line_raw == NULL) {
                return HTP_ERROR;
            }

            htp_chomp(connp->in_line, &connp->in_line_len);
            connp->in_tx->request_line = bstr_dup_ex(connp->in_tx->request_line_raw, 0, connp->in_line_len);
            if (connp->in_tx->request_line == NULL) {
                return HTP_ERROR;
            }

            // Parse request line
            if (connp->cfg->parse_request_line(connp) != HTP_OK) {
                // Note: downstream responsible for error logging
                return HTP_ERROR;
            }

            // Finalize request line parsing

            if (htp_tx_state_request_line(connp->in_tx) != HTP_OK) {
                return HTP_ERROR;
            }

            // Clean up.
            connp->in_line_len = 0;



            return HTP_OK;
        }
    }

    return HTP_ERROR;
}

htp_status_t htp_connp_REQ_FINALIZE(htp_connp_t *connp) {
    int rc = htp_tx_state_request_complete(connp->in_tx);
    if (rc != HTP_OK) return rc;

    // We're done with this request
    connp->in_state = htp_connp_REQ_IDLE;
    connp->in_tx = NULL;

    return HTP_OK;
}

/**
 * The idle state is invoked before and after every transaction. Consequently,
 * it will start a new transaction when data is available and finalise a transaction
 * which has been processed.
 *
 * @param[in] connp
 * @returns HTP_OK on state change, HTP_ERROR on error, or HTP_DATA when more data is needed.
 */
htp_status_t htp_connp_REQ_IDLE(htp_connp_t * connp) {
    // We want to start parsing the next request (and change
    // the state from IDLE) only if there's at least one
    // byte of data available. Otherwise we could be creating
    // new structures even if there's no more data on the
    // connection.
    IN_TEST_NEXT_BYTE_OR_RETURN(connp);

    connp->in_tx = htp_connp_tx_create(connp);
    if (connp->in_tx == NULL) return HTP_ERROR;

    // Change state to TRANSACTION_START
    htp_tx_state_request_start(connp->in_tx);

    return HTP_OK;
}

/**
 * Returns how many bytes from the current data chunks were consumed so far.
 *
 * @param[in] connp
 * @return The number of bytes consumed.
 */
size_t htp_connp_req_data_consumed(htp_connp_t *connp) {
    return connp->in_current_offset;
}

int htp_connp_req_data(htp_connp_t *connp, const htp_time_t *timestamp, const void *data, size_t len) {
    #ifdef HTP_DEBUG
    fprintf(stderr, "htp_connp_req_data(connp->in_status %x)\n", connp->in_status);
    fprint_raw_data(stderr, __FUNCTION__, data, len);
    #endif

    // Return if the connection is in stop state.
    if (connp->in_status == HTP_STREAM_STOP) {
        htp_log(connp, HTP_LOG_MARK, HTP_LOG_INFO, 0, "Inbound parser is in HTP_STREAM_STOP");

        return HTP_STREAM_STOP;
    }

    // Return if the connection had a fatal error earlier
    if (connp->in_status == HTP_STREAM_ERROR) {
        htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0, "Inbound parser is in HTP_STREAM_ERROR");

        #ifdef HTP_DEBUG
        fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_DATA (previous error)\n");
        #endif

        return HTP_STREAM_ERROR;
    }

    // If the length of the supplied data chunk is zero, proceed
    // only if the stream has been closed. We do not allow zero-sized
    // chunks in the API, but we use them internally to force the parsers
    // to finalize parsing.
    if ((len == 0) && (connp->in_status != HTP_STREAM_CLOSED)) {
        htp_log(connp, HTP_LOG_MARK, HTP_LOG_ERROR, 0, "Zero-length data chunks are not allowed");

        #ifdef HTP_DEBUG
        fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_DATA (zero-length chunk)\n");
        #endif

        return HTP_STREAM_CLOSED;
    }

    // Remember the timestamp of the current request data chunk
    if (timestamp != NULL) {
        memcpy(&connp->in_timestamp, timestamp, sizeof (*timestamp));
    }

    // Store the current chunk information    
    connp->in_current_data = (unsigned char *)data;
    connp->in_current_len = len;
    connp->in_current_offset = 0;
    connp->in_chunk_count++;

    htp_conn_track_inbound_data(connp->conn, len, timestamp);


    // Return without processing any data if the stream is in tunneling
    // mode (which it would be after an initial CONNECT transaction).
    if (connp->in_status == HTP_STREAM_TUNNEL) {
        #ifdef HTP_DEBUG
        fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_TUNNEL\n");
        #endif

        return HTP_STREAM_TUNNEL;
    }

    if (connp->out_status == HTP_STREAM_DATA_OTHER) {
        connp->out_status = HTP_STREAM_DATA;
    }

    // Invoke a processor, in a loop, until an error
    // occurs or until we run out of data. Many processors
    // will process a request, each pointing to the next
    // processor that needs to run.
    for (;;) {
        #ifdef HTP_DEBUG
        fprintf(stderr, "htp_connp_req_data: in state=%s, progress=%s\n",
                htp_connp_in_state_as_string(connp),
                htp_tx_progress_as_string(connp->in_tx));
        #endif

        // Return if there's been an error
        // or if we've run out of data. We are relying
        // on processors to add error messages, so we'll
        // keep quiet here.
        int rc = connp->in_state(connp);
        if (rc == HTP_OK) {
            if (connp->in_status == HTP_STREAM_TUNNEL) {
                #ifdef HTP_DEBUG
                fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_TUNNEL\n");
                #endif

                return HTP_STREAM_TUNNEL;
            }
        } else {
            // Do we need more data?
            if (rc == HTP_DATA) {
                #ifdef HTP_DEBUG
                fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_DATA\n");
                #endif

                connp->in_status = HTP_STREAM_DATA;

                return HTP_STREAM_DATA;
            }

            // Check for suspended parsing
            if (rc == HTP_DATA_OTHER) {
                // We might have actually consumed the entire data chunk?
                if (connp->in_current_offset >= connp->in_current_len) {
                    // Do not send STREAM_DATE_DATA_OTHER if we've
                    // consumed the entire chunk
                    #ifdef HTP_DEBUG
                    fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_DATA (suspended parsing)\n");
                    #endif

                    connp->in_status = HTP_STREAM_DATA;

                    return HTP_STREAM_DATA;
                } else {
                    // Partial chunk consumption
                    #ifdef HTP_DEBUG
                    fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_DATA_OTHER\n");
                    #endif

                    connp->in_status = HTP_STREAM_DATA_OTHER;

                    return HTP_STREAM_DATA_OTHER;
                }
            }

            // Check for stop
            if (rc == HTP_STOP) {
                #ifdef HTP_DEBUG
                fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_STOP\n");
                #endif

                connp->in_status = HTP_STREAM_STOP;

                return HTP_STREAM_STOP;
            }

            // If we're here that means we've encountered an error.
            connp->in_status = HTP_STREAM_ERROR;

            #ifdef HTP_DEBUG
            fprintf(stderr, "htp_connp_req_data: returning HTP_STREAM_ERROR (state response)\n");
            #endif

            return HTP_STREAM_ERROR;
        }
    }

    return HTP_STREAM_ERROR;
}
