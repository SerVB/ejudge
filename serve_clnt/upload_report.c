/* -*- mode: c; coding: koi8-r -*- */
/* $Id$ */

/* Copyright (C) 2004 Alexander Chernov <cher@ispras.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "serve_clnt.h"
#include "protocol.h"
#include "pathutl.h"

#include <reuse/xalloc.h>

#include <stdlib.h>

int
serve_clnt_upload_report(int sock_fd,
                         int cmd,
                         int user_id,
                         int contest_id,
                         int run_id,
                         unsigned int flags,
                         size_t report_size,
                         const unsigned char *report_data)
{
  struct prot_serve_pkt_upload_report *out = 0;
  struct prot_serve_packet *in = 0;
  size_t out_size = 0;
  int in_size = 0, r;
  void *void_in = 0;

  if (cmd != SRV_CMD_UPLOAD_REPORT) return -SRV_ERR_PROTOCOL;
  if (sock_fd < 0) return -SRV_ERR_NOT_CONNECTED;

  out_size = sizeof(*out) + report_size;
  out = alloca(out_size);
  memset(out, 0, out_size);

  out->b.id = cmd;
  out->b.magic = PROT_SERVE_PACKET_MAGIC;
  out->user_id = user_id;
  out->contest_id = contest_id;
  out->run_id = run_id;
  out->flags = flags;
  out->report_size = report_size;
  memcpy(out->data, report_data, report_size);

  if ((r = serve_clnt_send_packet(sock_fd, out_size, out)) < 0) {
    return r;
  }
  if ((r = serve_clnt_recv_packet(sock_fd, &in_size, &void_in)) < 0) {
    return r;
  }
  in = void_in;
  if (in_size != sizeof(*in)) {
    xfree(in);
    err("serve_clnt_upload_report: unexpected reply length %d", in_size);
    return -SRV_ERR_PROTOCOL;
  }
  if (in->id < 0) {
    r = in->id;
    xfree(in);
    return r;
  }
  if (in->id != SRV_RPL_OK) {
    xfree(in);
    err("serve_clnt_submit_run: unexpected reply: %d", in->id);
    return -SRV_ERR_PROTOCOL;
  }
  xfree(in);
  return SRV_RPL_OK;
}

/**
 * Local variables:
 *  compile-command: "make"
 *  c-font-lock-extra-types: ("\\sw+_t" "FILE")
 * End:
 */
