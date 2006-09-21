/* -*- mode: c -*- */
/* $Id$ */

/* Copyright (C) 2006 Alexander Chernov <cher@ejudge.ru> */

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

#include "serve_state.h"
#include "runlog.h"
#include "prepare.h"
#include "l10n.h"
#include "html.h"
#include "errlog.h"
#include "protocol.h"
#include "clarlog.h"
#include "fileutl.h"
#include "teamdb.h"
#include "contests.h"
#include "job_packet.h"
#include "archive_paths.h"
#include "xml_utils.h"
#include "compile_packet.h"
#include "run_packet.h"
#include "curtime.h"
#include "userlist.h"

#include <reuse/logger.h>
#include <reuse/xalloc.h>
#include <reuse/osdeps.h>

#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/time.h>

void
serve_update_standings_file(serve_state_t state,
                            const struct contest_desc *cnts,
                            int force_flag)
{
  time_t start_time, stop_time, duration;
  int p = 0;

  run_get_times(state->runlog_state, &start_time, 0, &duration, &stop_time);

  while (1) {
    if (state->global->virtual) break;
    if (force_flag) break;
    if (!state->global->autoupdate_standings) return;
    if (!duration) break;
    if (!state->global->board_fog_time) break;

    ASSERT(state->current_time >= start_time);
    ASSERT(state->global->board_fog_time >= 0);
    ASSERT(state->global->board_unfog_time >= 0);
    
    p = run_get_fog_period(state->runlog_state, state->current_time,
                           state->global->board_fog_time,
                           state->global->board_unfog_time);
    if (p == 1) return;
    break;
  }

  if (!state->global->virtual) {
    p = run_get_fog_period(state->runlog_state, state->current_time,
                           state->global->board_fog_time,
                           state->global->board_unfog_time);
  }
  l10n_setlocale(state->global->standings_locale_id);
  write_standings(state, cnts, state->global->status_dir,
                  state->global->standings_file_name,
                  state->global->users_on_page,
                  state->global->stand_header_txt,
                  state->global->stand_footer_txt,
                  state->accepting_mode);
  if (state->global->stand2_file_name[0]) {
    write_standings(state, cnts, state->global->status_dir,
                    state->global->stand2_file_name, 0,
                    state->global->stand2_header_txt,
                    state->global->stand2_footer_txt,
                    state->accepting_mode);
  }
  l10n_setlocale(0);
  if (state->global->virtual) return;
  switch (p) {
  case 0:
    state->global->start_standings_updated = 1;
    break;
  case 1:
    state->global->fog_standings_updated = 1;
    break;
  case 2:
    state->global->unfog_standings_updated = 1;
    break;
  }
}

void
serve_update_public_log_file(serve_state_t state,
                             const struct contest_desc *cnts)
{
  time_t start_time, stop_time, duration;
  int p;

  if (!state->global->plog_update_time) return;
  if (state->current_time < state->last_update_public_log + state->global->plog_update_time) return;

  run_get_times(state->runlog_state, &start_time, 0, &duration, &stop_time);

  while (1) {
    if (!duration) break;
    if (!state->global->board_fog_time) break;

    ASSERT(state->current_time >= start_time);
    ASSERT(state->global->board_fog_time >= 0);
    ASSERT(state->global->board_unfog_time >= 0);
    
    p = run_get_fog_period(state->runlog_state, state->current_time,
                           state->global->board_fog_time,
                           state->global->board_unfog_time);
    if (p == 1) return;
    break;
  }

  l10n_setlocale(state->global->standings_locale_id);
  write_public_log(state, cnts, state->global->status_dir,
                   state->global->plog_file_name,
                   state->global->plog_header_txt,
                   state->global->plog_footer_txt);
  state->last_update_public_log = state->current_time;
  l10n_setlocale(0);
}

static void
do_update_xml_log(serve_state_t state, const struct contest_desc *cnts,
                  char const *name, int external_mode)
{
  struct run_header rhead;
  int rtotal;
  const struct run_entry *rentries;
  path_t path1;
  path_t path2;
  FILE *fout;

  run_get_header(state->runlog_state, &rhead);
  rtotal = run_get_total(state->runlog_state);
  rentries = run_get_entries_ptr(state->runlog_state);

  snprintf(path1, sizeof(path1), "%s/in/%s.tmp",state->global->status_dir,name);
  snprintf(path2, sizeof(path2), "%s/dir/%s", state->global->status_dir, name);

  if (!(fout = fopen(path1, "w"))) {
    err("update_xml_log: cannot open %s", path1);
    return;
  }
  unparse_runlog_xml(state, cnts, fout, &rhead, rtotal, rentries,
                     external_mode, state->current_time);
  if (ferror(fout)) {
    err("update_xml_log: write error");
    fclose(fout);
    unlink(path1);
    return;
  }
  if (fclose(fout) < 0) {
    err("update_xml_log: write error");
    unlink(path1);
    return;
  }
  if (rename(path1, path2) < 0) {
    err("update_xml_log: rename %s -> %s failed", path1, path2);
    unlink(path1);
    return;
  }
}

void
serve_update_external_xml_log(serve_state_t state,
                              const struct contest_desc *cnts)
{
  if (!state->global->external_xml_update_time) return;
  if (state->current_time < state->last_update_external_xml_log + state->global->external_xml_update_time) return;
  state->last_update_external_xml_log = state->current_time;
  do_update_xml_log(state, cnts, "external.xml", 1);
}

void
serve_update_internal_xml_log(serve_state_t state,
                              const struct contest_desc *cnts)
{
  if (!state->global->internal_xml_update_time) return;
  if (state->current_time < state->last_update_internal_xml_log + state->global->internal_xml_update_time) return;
  state->last_update_internal_xml_log = state->current_time;
  do_update_xml_log(state, cnts, "internal.xml", 0);
}

int
serve_update_status_file(serve_state_t state, int force_flag)
{
  struct prot_serve_status_v2 status;
  time_t t1, t2, t3, t4;
  int p;

  if (!force_flag && state->current_time <= state->last_update_status_file) return 0;

  memset(&status, 0, sizeof(status));
  status.magic = PROT_SERVE_STATUS_MAGIC_V2;

  status.cur_time = state->current_time;
  run_get_times(state->runlog_state, &t1, &t2, &t3, &t4);
  status.start_time = t1;
  status.sched_time = t2;
  status.duration = t3;
  status.stop_time = t4;
  status.total_runs = run_get_total(state->runlog_state);
  status.total_clars = clar_get_total(state->clarlog_state);
  status.clars_disabled = state->global->disable_clars;
  status.team_clars_disabled = state->global->disable_team_clars;
  status.score_system = state->global->score_system_val;
  status.clients_suspended = state->clients_suspended;
  status.testing_suspended = state->testing_suspended;
  status.download_interval = state->global->team_download_time / 60;
  status.is_virtual = state->global->virtual;
  status.continuation_enabled = state->global->enable_continue;
  status.printing_enabled = state->global->enable_printing;
  status.printing_suspended = state->printing_suspended;
  status.always_show_problems = state->global->always_show_problems;
  status.accepting_mode = state->accepting_mode;
  if (status.start_time && status.duration && state->global->board_fog_time > 0
      && !status.is_virtual) {
    status.freeze_time = status.start_time + status.duration - state->global->board_fog_time;
    if (status.freeze_time < status.start_time) {
      status.freeze_time = status.start_time;
    }
  }
  if (!status.duration && state->global->contest_finish_time_d)
    status.finish_time = state->global->contest_finish_time_d;
  //if (status.duration) status.continuation_enabled = 0;

  if (!state->global->virtual) {
    p = run_get_fog_period(state->runlog_state, state->current_time,
                           state->global->board_fog_time, state->global->board_unfog_time);
    if (p == 1 && state->global->autoupdate_standings) {
      status.standings_frozen = 1;
    }
  }

  status.stat_reported_before = state->stat_reported_before;
  status.stat_report_time = state->stat_report_time;

  generic_write_file((char*) &status, sizeof(status), SAFE,
                     state->global->status_dir, "status", "");
  state->last_update_status_file = state->current_time;
  return 1;
}

void
serve_load_status_file(serve_state_t state)
{
  struct prot_serve_status_v2 status;
  size_t stat_len = 0;
  char *ptr = 0;

  if (generic_read_file(&ptr, 0, &stat_len, 0, state->global->status_dir,
                        "dir/status", "") < 0) {
    if (state->global->score_system_val == SCORE_OLYMPIAD)
      state->accepting_mode = 1;
    return;
  }
  if (stat_len != sizeof(status)) {
    info("load_status_file: length %zu does not match %zu",
         stat_len, sizeof(status));
    xfree(ptr);
    if (state->global->score_system_val == SCORE_OLYMPIAD)
      state->accepting_mode = 1;
    return;
  }
  memcpy(&status, ptr, sizeof(status));
  xfree(ptr);
  if (status.magic != PROT_SERVE_STATUS_MAGIC_V2) {
    info("load_status_file: bad magic value");
    if (state->global->score_system_val == SCORE_OLYMPIAD)
      state->accepting_mode = 1;
    return;
  }

  state->clients_suspended = status.clients_suspended;
  info("load_status_file: clients_suspended = %d", state->clients_suspended);
  state->testing_suspended = status.testing_suspended;
  info("load_status_file: testing_suspended = %d", state->testing_suspended);
  state->accepting_mode = status.accepting_mode;
  info("load_status_file: accepting_mode = %d", state->accepting_mode);
  state->printing_suspended = status.printing_suspended;
  info("load_status_file: printing_suspended = %d", state->printing_suspended);
  state->stat_reported_before = status.stat_reported_before;
  state->stat_report_time = status.stat_report_time;
}

int
serve_check_user_quota(serve_state_t state, int user_id, size_t size)
{
  int num;
  size_t total;

  if (size > state->global->max_run_size) return -1;
  run_get_team_usage(state->runlog_state, user_id, &num, &total);
  if (num > state->global->max_run_num
      || total + size > state->global->max_run_total)
    return -1;
  return 0;
}

int
serve_check_clar_quota(serve_state_t state, int user_id, size_t size)
{
  int num;
  size_t total;

  if (size > state->global->max_clar_size) return -1;
  clar_get_team_usage(state->clarlog_state, user_id, &num, &total);
  if (num > state->global->max_clar_num
      || total + size > state->global->max_clar_total)
    return -1;
  return 0;
}

int
serve_check_cnts_caps(serve_state_t state,
                      const struct contest_desc *cnts,
                      int user_id, int bit)
{
  opcap_t caps;
  unsigned char const *login = 0;

  login = teamdb_get_login(state->teamdb_state, user_id);
  if (!login || !*login) return 0;

  if (opcaps_find(&cnts->capabilities, login, &caps) < 0) return 0;
  if (opcaps_check(caps, bit) < 0) return 0;
  return 1;
}

int
serve_get_cnts_caps(serve_state_t state,
                    const struct contest_desc *cnts,
                    int user_id, opcap_t *out_caps)
{
  opcap_t caps;
  unsigned char const *login = 0;

  login = teamdb_get_login(state->teamdb_state, user_id);
  if (!login || !*login) return -1;

  if (opcaps_find(&cnts->capabilities, login, &caps) < 0) return -1;
  if (out_caps) *out_caps = caps;
  return 0;
}

static int
do_build_compile_dirs(serve_state_t state,
                      const unsigned char *status_dir,
                      const unsigned char *report_dir)
{
  int i;

  if (!status_dir || !*status_dir || !report_dir || !*report_dir) abort();

  for (i = 0; i < state->compile_dirs_u; i++)
    if (!strcmp(state->compile_dirs[i].status_dir, status_dir))
      break;
  if (i < state->compile_dirs_u) return i;

  if (state->compile_dirs_u == state->compile_dirs_a) {
    if (!state->compile_dirs_a) state->compile_dirs_a = 8;
    state->compile_dirs_a *= 2;
    XREALLOC(state->compile_dirs, state->compile_dirs_a);
  }

  state->compile_dirs[state->compile_dirs_u].status_dir = xstrdup(status_dir);
  state->compile_dirs[state->compile_dirs_u].report_dir = xstrdup(report_dir);
  return state->compile_dirs_u++;
}

void
serve_build_compile_dirs(serve_state_t state)
{
  int i;

  for (i = 1; i <= state->max_lang; i++) {
    if (!state->langs[i]) continue;
    do_build_compile_dirs(state,
                          state->langs[i]->compile_status_dir,
                          state->langs[i]->compile_report_dir);
  }
}

static int
do_build_run_dirs(serve_state_t state,
                  const unsigned char *status_dir,
                  const unsigned char *report_dir,
                  const unsigned char *team_report_dir,
                  const unsigned char *full_report_dir)
{
  int i;

  if (!status_dir || !*status_dir) abort();

  for (i = 0; i < state->run_dirs_u; i++)
    if (!strcmp(state->run_dirs[i].status_dir, status_dir))
      break;
  if (i < state->run_dirs_u) return i;

  if (state->run_dirs_u == state->run_dirs_a) {
    if (!state->run_dirs_a) state->run_dirs_a = 8;
    state->run_dirs_a *= 2;
    XREALLOC(state->run_dirs, state->run_dirs_a);
  }

  state->run_dirs[state->run_dirs_u].status_dir = xstrdup(status_dir);
  state->run_dirs[state->run_dirs_u].report_dir = xstrdup(report_dir);
  state->run_dirs[state->run_dirs_u].team_report_dir = xstrdup(team_report_dir);
  state->run_dirs[state->run_dirs_u].full_report_dir = xstrdup(full_report_dir);
  return state->run_dirs_u++;
}

void
serve_build_run_dirs(serve_state_t state)
{
  int i;

  for (i = 1; i <= state->max_tester; i++) {
    if (!state->testers[i]) continue;
    do_build_run_dirs(state, state->testers[i]->run_status_dir,
                      state->testers[i]->run_report_dir,
                      state->testers[i]->run_team_report_dir,
                      state->testers[i]->run_full_archive_dir);
  }
}

int
serve_create_symlinks(serve_state_t state)
{
  unsigned char src_path[PATH_MAX];
  unsigned char dst_path[PATH_MAX];
  path_t stand_file;
  int npages, pgn;

  if (state->global->stand_symlink_dir[0] && state->global->htdocs_dir[0]) {
    if (state->global->users_on_page > 0) {
      // FIXME: check, that standings_file_name depends on page number
      npages = (teamdb_get_total_teams(state->teamdb_state)
                + state->global->users_on_page - 1)
        / state->global->users_on_page;
      for (pgn = 0; pgn < npages; pgn++) {
        if (!pgn) {
          snprintf(stand_file, sizeof(stand_file),
                   state->global->standings_file_name, pgn + 1);
        } else {
          snprintf(stand_file, sizeof(stand_file),
                   state->global->stand_file_name_2, pgn + 1);
        }
        snprintf(src_path, sizeof(src_path), "%s/dir/%s",
                 state->global->status_dir, stand_file);
        snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
                 state->global->htdocs_dir, state->global->stand_symlink_dir,
                 stand_file);
        os_normalize_path(dst_path);
        if (unlink(dst_path) < 0 && errno != ENOENT) {
          err("unlink %s failed: %s", dst_path, os_ErrorMsg());
          //return -1;
        }
        if (symlink(src_path, dst_path) < 0) {
          err("symlink %s->%s failed: %s", dst_path, src_path, os_ErrorMsg());
          //return -1;
        }
      }
    } else {
      snprintf(src_path, sizeof(src_path), "%s/dir/%s",
               state->global->status_dir, state->global->standings_file_name);
      snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
               state->global->htdocs_dir, state->global->stand_symlink_dir,
               state->global->standings_file_name);
      os_normalize_path(dst_path);
      if (unlink(dst_path) < 0 && errno != ENOENT) {
        err("unlink %s failed: %s", dst_path, os_ErrorMsg());
        //return -1;
      }
      if (symlink(src_path, dst_path) < 0) {
        err("symlink %s->%s failed: %s", dst_path, src_path, os_ErrorMsg());
        //return -1;
      }
    }
  }
  if (state->global->stand2_symlink_dir[0] && state->global->htdocs_dir[0]
      && state->global->stand2_file_name[0]) {
    snprintf(src_path, sizeof(src_path), "%s/dir/%s",
             state->global->status_dir, state->global->stand2_file_name);
    snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
             state->global->htdocs_dir, state->global->stand2_symlink_dir,
             state->global->stand2_file_name);
    os_normalize_path(dst_path);
    if (unlink(dst_path) < 0 && errno != ENOENT) {
      err("unlink %s failed: %s", dst_path, os_ErrorMsg());
      //return -1;
    }
    if (symlink(src_path, dst_path) < 0) {
      err("symlink %s->%s failed: %s", dst_path, src_path, os_ErrorMsg());
      //return -1;
    }
  }
  if (state->global->plog_symlink_dir[0] && state->global->htdocs_dir[0]
      && state->global->plog_file_name[0]
      && state->global->plog_update_time > 0) {
    snprintf(src_path, sizeof(src_path), "%s/dir/%s",
             state->global->status_dir, state->global->plog_file_name);
    snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
             state->global->htdocs_dir, state->global->plog_symlink_dir,
             state->global->plog_file_name);
    os_normalize_path(dst_path);
    if (unlink(dst_path) < 0 && errno != ENOENT) {
      err("unlink %s failed: %s", dst_path, os_ErrorMsg());
      //return -1;
    }
    if (symlink(src_path, dst_path) < 0) {
      err("symlink %s->%s failed: %s", dst_path, src_path, os_ErrorMsg());
      //return -1;
    }
  }
  return 0;
}

const unsigned char *
serve_get_email_sender(const struct contest_desc *cnts)
{
  int sysuid;
  struct passwd *ppwd;

  if (cnts && cnts->register_email) return cnts->register_email;
  sysuid = getuid();
  ppwd = getpwuid(sysuid);
  return ppwd->pw_name;
}

static void
generate_statistics_email(serve_state_t state, const struct contest_desc *cnts,
                          time_t from_time, time_t to_time)
{
  unsigned char esubj[1024];
  struct tm *ptm;
  char *etxt = 0, *ftxt = 0;
  size_t elen = 0, flen = 0;
  FILE *eout = 0, *fout = 0;
  const unsigned char *mail_args[7];
  const unsigned char *originator;
  struct tm tm1;

  ptm = localtime(&from_time);
  snprintf(esubj, sizeof(esubj),
           "Daily statistics for %04d/%02d/%02d, contest %d",
           ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
           state->global->contest_id);

  eout = open_memstream(&etxt, &elen);
  generate_daily_statistics(state, eout, from_time, to_time);
  fclose(eout); eout = 0;
  if (!etxt || !*etxt) {
    xfree(etxt);
    return;
  }

  localtime_r(&from_time, &tm1);

  fout = open_memstream(&ftxt, &flen);
  fprintf(fout,
          "Hello,\n"
          "\n"
          "This is daily report for contest %d (%s)\n"
          "Report day: %04d/%02d/%02d\n\n"
          "%s\n\n"
          "-\n"
          "Regards,\n"
          "the ejudge contest management system\n",
          state->global->contest_id, cnts->name,
          tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday,
          etxt);
  fclose(fout); fout = 0;

  originator = serve_get_email_sender(cnts);
  mail_args[0] = "mail";
  mail_args[1] = "";
  mail_args[2] = esubj;
  mail_args[3] = originator;
  mail_args[4] = cnts->daily_stat_email;
  mail_args[5] = ftxt;
  mail_args[6] = 0;
  send_job_packet(NULL, (unsigned char **) mail_args);
  xfree(ftxt); ftxt = 0;
  xfree(etxt); etxt = 0;
}

void
serve_check_stat_generation(serve_state_t state,
                            const struct contest_desc *cnts,
                            int force_flag)
{
  struct tm *ptm;
  time_t thisday, nextday;

  if (!force_flag && state->stat_last_check_time > 0
      && state->stat_last_check_time + 600 > state->current_time)
    return;
  state->stat_last_check_time = state->current_time;
  if (!cnts->daily_stat_email) return;

  if (!state->stat_reported_before) {
    // set the time to the beginning of this day
    ptm = localtime(&state->current_time);
    ptm->tm_hour = 0;
    ptm->tm_min = 0;
    ptm->tm_sec = 0;
    ptm->tm_isdst = -1;
    if ((thisday = mktime(ptm)) == (time_t) -1) {
      err("check_stat_generation: mktime() failed");
      thisday = 0;
    }
    state->stat_reported_before = thisday;
  }
  if (!state->stat_report_time) {
    // set the time to the beginning of the next day
    ptm = localtime(&state->current_time);
    ptm->tm_hour = 0;
    ptm->tm_min = 0;
    ptm->tm_sec = 0;
    ptm->tm_isdst = -1;
    ptm->tm_mday++;             // pretty valid. see man mktime
    if ((nextday = mktime(ptm)) == (time_t) -1) {
      err("check_stat_generation: mktime() failed");
      nextday = 0;
    }
    state->stat_report_time = nextday;
  }

  if (state->current_time < state->stat_report_time) return;

  // generate report for each day from stat_reported_before to stat_report_time
  thisday = state->stat_reported_before;
  while (thisday < state->stat_report_time) {
    ptm = localtime(&thisday);
    ptm->tm_hour = 0;
    ptm->tm_min = 0;
    ptm->tm_sec = 0;
    ptm->tm_isdst = -1;
    ptm->tm_mday++;
    if ((nextday = mktime(ptm)) == (time_t) -1) {
      err("check_stat_generation: mktime() failed");
      state->stat_reported_before = 0;
      state->stat_report_time = 0;
      return;
    }
    generate_statistics_email(state, cnts, thisday, nextday);
    thisday = nextday;
  }

  ptm = localtime(&thisday);
  ptm->tm_hour = 0;
  ptm->tm_min = 0;
  ptm->tm_sec = 0;
  ptm->tm_isdst = -1;
  ptm->tm_mday++;
  if ((nextday = mktime(ptm)) == (time_t) -1) {
    err("check_stat_generation: mktime() failed");
    state->stat_reported_before = 0;
    state->stat_report_time = 0;
    return;
  }
  state->stat_reported_before = thisday;
  state->stat_report_time = nextday;
}

void
serve_move_files_to_insert_run(serve_state_t state, int run_id)
{
  int total = run_get_total(state->runlog_state);
  int i, s;
  const struct section_global_data *global = state->global;

  if (run_id >= total - 1) return;
  for (i = total - 1; i >= run_id; i--) {
    archive_remove(state, global->run_archive_dir, i + 1, 0);
    archive_remove(state, global->xml_report_archive_dir, i + 1, 0);
    archive_remove(state, global->report_archive_dir, i + 1, 0);
    if (global->team_enable_rep_view) {
      archive_remove(state, global->team_report_archive_dir, i + 1, 0);
    }
    if (global->enable_full_archive) {
      archive_remove(state, global->full_archive_dir, i + 1, 0);
    }
    archive_remove(state, global->audit_log_dir, i + 1, 0);
    s = run_get_status(state->runlog_state, i);
    if (s >= RUN_PSEUDO_FIRST && s <= RUN_PSEUDO_LAST) continue;
    archive_rename(state, global->run_archive_dir, 0, i, 0, i + 1, 0, 0);
    archive_rename(state, global->xml_report_archive_dir, 0, i, 0, i + 1, 0, 0);
    archive_rename(state, global->report_archive_dir, 0, i, 0, i + 1, 0, 0);
    if (global->team_enable_rep_view) {
      archive_rename(state, global->team_report_archive_dir, 0,i,0,i + 1,0,0);
    }
    if (global->enable_full_archive) {
      archive_rename(state, global->full_archive_dir, 0, i, 0, i + 1, 0, 0);
    }
    archive_rename(state, global->audit_log_dir, 0, i, 0, i + 1, 0, 0);
  }

  /* FIXME: add audit information for all the renamed runs */
}

void
serve_audit_log(serve_state_t state, int run_id, int user_id,
                ej_ip_t ip, int ssl_flag, const char *format, ...)
{
  unsigned char buf[16384];
  unsigned char tbuf[128];
  va_list args;
  struct tm *ltm;
  path_t audit_path;
  FILE *f;
  unsigned char *login;
  size_t buf_len;

  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  buf_len = strlen(buf);
  while (buf_len > 0 && isspace(buf[buf_len - 1])) buf[--buf_len] = 0;

  ltm = localtime(&state->current_time);
  snprintf(tbuf, sizeof(tbuf), "%04d/%02d/%02d %02d:%02d:%02d",
           ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
           ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

  archive_make_write_path(state, audit_path, sizeof(audit_path),
                          state->global->audit_log_dir, run_id, 0, 0);
  if (archive_dir_prepare(state, state->global->audit_log_dir,
                          run_id, 0, 1) < 0) return;
  if (!(f = fopen(audit_path, "a"))) return;

  fprintf(f, "Date: %s\n", tbuf);
  if (!user_id) {
    fprintf(f, "From: SYSTEM\n");
  } else if (user_id <= 0) {
    fprintf(f, "From: unauthentificated user\n");
  } else if (!(login = teamdb_get_login(state->teamdb_state, user_id))){
    fprintf(f, "From: user %d (login unknown)\n", user_id);
  } else {
    fprintf(f, "From: %s (uid %d)\n", login, user_id);
  }
  if (ip) {
    fprintf(f, "Ip: %s%s\n", xml_unparse_ip(ip), ssl_flag?"/SSL":"");
  }
  fprintf(f, "%s\n\n", buf);

  fclose(f);
}

static const unsigned char b32_digits[]=
"0123456789ABCDEFGHIJKLMNOPQRSTUV";
static void
b32_number(unsigned long long num, unsigned char buf[])
{
  int i;

  memset(buf, '0', SERVE_PACKET_NAME_SIZE - 1);
  buf[SERVE_PACKET_NAME_SIZE - 1] = 0;
  i = SERVE_PACKET_NAME_SIZE - 2;
  while (num > 0 && i >= 0) {
    buf[i] = b32_digits[num & 0x1f];
    i--;
    num >>= 5;
  }
  ASSERT(!num);
}

void
serve_packet_name(int run_id, int prio, unsigned char buf[])
{
  unsigned long long num = 0;
  struct timeval ts;

  // generate "random" number, that would include the
  // pid of "serve", the current time (with microseconds)
  // and some small random component.
  // pid is 2 byte (15 bit)
  // run_id is 2 byte
  // time_t component - 4 byte
  // nanosec component - 4 byte

  num = (getpid() & 0x7fffLLU) << 25LLU;
  num |= (run_id & 0x7fffLLU) << 40LLU;
  gettimeofday(&ts, 0);
  num |= (ts.tv_sec ^ ts.tv_usec) & 0x1ffffff;
  b32_number(num, buf);
  if (prio < -16) prio = -16;
  if (prio > 15) prio = 15;
  buf[0] = b32_digits[prio + 16];
}

int
serve_compile_request(serve_state_t state,
                      unsigned char const *str, int len,
                      int run_id, int lang_id, int locale_id, int output_only,
                      unsigned char const *sfx,
                      char **compiler_env,
                      int accepting_mode,
                      int priority_adjustment)
{
  struct compile_run_extra rx;
  struct compile_request_packet cp;
  void *pkt_buf = 0;
  size_t pkt_len = 0;
  unsigned char pkt_name[SERVE_PACKET_NAME_SIZE];
  int arch_flags;
  path_t run_arch;
  const struct section_global_data *global = state->global;

  if (accepting_mode == -1) accepting_mode = state->accepting_mode;

  memset(&cp, 0, sizeof(cp));
  cp.judge_id = state->compile_request_id++;
  cp.contest_id = global->contest_id;
  cp.run_id = run_id;
  cp.lang_id = lang_id;
  cp.locale_id = locale_id;
  cp.output_only = output_only;
  get_current_time(&cp.ts1, &cp.ts1_us);
  cp.run_block_len = sizeof(rx);
  cp.run_block = &rx;
  cp.env_num = -1;
  cp.env_vars = (unsigned char**) compiler_env;

  memset(&rx, 0, sizeof(rx));
  rx.accepting_mode = accepting_mode;
  rx.priority_adjustment = priority_adjustment;

  if (compile_request_packet_write(&cp, &pkt_len, &pkt_buf) < 0) {
    // FIXME: need reasonable recovery?
    goto failed;
  }

  if (!sfx) sfx = "";
  serve_packet_name(run_id, 0, pkt_name);

  if (len == -1) {
    // copy from archive
    arch_flags = archive_make_read_path(state, run_arch, sizeof(run_arch),
                                        global->run_archive_dir, run_id, 0,0);
    if (arch_flags < 0) return -1;
    if (generic_copy_file(arch_flags, 0, run_arch, "",
                          0, global->compile_src_dir, pkt_name, sfx) < 0)
      goto failed;
  } else {
    // write from memory
    if (generic_write_file(str, len, 0,
                           global->compile_src_dir, pkt_name, sfx) < 0)
      goto failed;
  }

  if (generic_write_file(pkt_buf, pkt_len, SAFE,
                         global->compile_queue_dir, pkt_name, "") < 0) {
    goto failed;
  }

  if (run_change_status(state->runlog_state, run_id, RUN_COMPILING, 0, -1,
                        cp.judge_id) < 0) {
    goto failed;
  }

  xfree(pkt_buf);
  return 0;

 failed:
  xfree(pkt_buf);
  return -1;
}

int
serve_run_request(serve_state_t state,
                  FILE *errf,
                  const unsigned char *run_text,
                  size_t run_size,
                  int run_id,
                  int user_id,
                  int prob_id,
                  int lang_id,
                  int variant,
                  int priority_adjustment,
                  int judge_id,
                  int accepting_mode,
                  const unsigned char *compile_report_dir,
                  const struct compile_reply_packet *comp_pkt)
{
  int cn;
  struct section_problem_data *prob;
  struct section_language_data *lang = 0;
  unsigned char *arch = "", *exe_sfx = "";
  const unsigned char *user_name;
  int prio, i;
  unsigned char pkt_base[SERVE_PACKET_NAME_SIZE];
  unsigned char exe_out_name[256];
  unsigned char exe_in_name[256];
  struct run_request_packet *run_pkt = 0;
  struct teamdb_export te;
  void *run_pkt_out = 0;
  size_t run_pkt_out_size = 0;

  if (prob_id <= 0 || prob_id > state->max_prob 
      || !(prob = state->probs[prob_id])) {
    fprintf(errf, "invalid problem %d", prob_id);
    return -1;
  }
  if (lang_id > 0) {
    if (lang_id > state->max_lang || !(lang = state->langs[lang_id])) {
      fprintf(errf, "invalid language %d", lang_id);
      return -1;
    }
  }
  if (!(user_name = teamdb_get_name(state->teamdb_state, user_id))) {
    fprintf(errf, "invalid user %d", user_id);
    return -1;
  }
  if (!*user_name) user_name = teamdb_get_login(state->teamdb_state, user_id);

  if (lang) arch = lang->arch;
  if (lang) exe_sfx = lang->exe_sfx;

  cn = find_tester(state, prob_id, arch);
  if (cn < 1 || cn > state->max_tester || !state->testers[cn]) {
    fprintf(errf, "no appropriate checker for <%s>, <%s>\n",
            prob->short_name, arch);
    return -1;
  }

  if (prob->variant_num <= 0 && variant > 0) {
    fprintf(errf, "variant is not allowed for this problem\n");
    return -1;
  }
  if (prob->variant_num > 0) {
    if (variant <= 0) variant = find_variant(state, user_id, prob_id);
    if (variant <= 0) {
      fprintf(errf, "no appropriate variant for <%s>, <%s>\n",
              user_name, prob->short_name);
      return -1;
    }
  }

  /* calculate a priority */
  prio = 0;
  if (lang) prio += lang->priority_adjustment;
  prio += prob->priority_adjustment;
  prio += find_user_priority_adjustment(state, user_id);
  prio += state->testers[cn]->priority_adjustment;
  prio += priority_adjustment;
  
  if (judge_id < 0) judge_id = state->compile_request_id++;
  if (accepting_mode < 0) accepting_mode = state->accepting_mode;

  /* generate a packet name */
  serve_packet_name(run_id, prio, pkt_base);
  snprintf(exe_out_name, sizeof(exe_out_name), "%s%s", pkt_base, exe_sfx);

  if (!run_text) {
    snprintf(exe_in_name, sizeof(exe_in_name), "%06d%s", run_id, exe_sfx);
    if (generic_copy_file(REMOVE, compile_report_dir, exe_in_name, "",
                          0, state->global->run_exe_dir,exe_out_name, "") < 0) {
      fprintf(errf, "copying failed");
      return -1;
    }
  } else {
    if (generic_write_file(run_text, run_size, 0,
                           state->global->run_exe_dir, exe_out_name, "") < 0) {
      fprintf(errf, "writing failed");
      return -1;
    }
  }

  /* create an internal representation of run packet */
  XALLOCAZ(run_pkt, 1);

  run_pkt->judge_id = judge_id;
  run_pkt->contest_id = state->global->contest_id;
  run_pkt->run_id = run_id;
  run_pkt->problem_id = prob->tester_id;
  run_pkt->accepting_mode = accepting_mode;
  run_pkt->scoring_system = state->global->score_system_val;
  run_pkt->variant = variant;
  run_pkt->accept_partial = prob->accept_partial;
  run_pkt->user_id = user_id;
  run_pkt->disable_sound = state->global->disable_sound;
  run_pkt->full_archive = state->global->enable_full_archive;
  run_pkt->memory_limit = state->global->enable_memory_limit_error;
  get_current_time(&run_pkt->ts4, &run_pkt->ts4_us);
  if (comp_pkt) {
    run_pkt->ts1 = comp_pkt->ts1;
    run_pkt->ts1_us = comp_pkt->ts1_us;
    run_pkt->ts2 = comp_pkt->ts2;
    run_pkt->ts2_us = comp_pkt->ts2_us;
    run_pkt->ts3 = comp_pkt->ts3;
    run_pkt->ts3_us = comp_pkt->ts3_us;
  } else {
    run_pkt->ts3 = run_pkt->ts4;
    run_pkt->ts3_us = run_pkt->ts4_us;
    run_pkt->ts2 = run_pkt->ts4;
    run_pkt->ts2_us = run_pkt->ts4_us;
    run_pkt->ts1 = run_pkt->ts4;
    run_pkt->ts1_us = run_pkt->ts4_us;
  }
  run_pkt->exe_sfx = exe_sfx;
  run_pkt->arch = arch;

  // process language-specific time adjustments
  if (prob->lang_time_adj) {
    size_t lsn = strlen(lang->short_name);
    size_t vl;
    int adj, n;
    unsigned char *sn;
    for (i = 0; (sn = prob->lang_time_adj[i]); i++) {
      vl = strlen(sn);
      if (vl > lsn + 1
          && !strncmp(sn, lang->short_name, lsn)
          && sn[lsn] == '='
          && sscanf(sn + lsn + 1, "%d%n", &adj, &n) == 1
          && !sn[lsn + 1 + n]
          && adj >= 0
          && adj <= 100) {
        run_pkt->time_limit_adj = adj;
      }
    }
  }

  /* in new binary packet format we don't care about neither "special"
   * characters in spellings nor about spelling length
   */
  teamdb_export_team(state->teamdb_state, user_id, &te);
  if (te.user && te.user->i.spelling && te.user->i.spelling[0]) {
    run_pkt->user_spelling = te.user->i.spelling;
  }
  if (!run_pkt->user_spelling && te.user && te.user->i.name
      && te.user->i.name[0]) {
    run_pkt->user_spelling = te.user->i.name;
  }
  if (!run_pkt->user_spelling && te.login && te.user->login
      && te.user->login[0]) {
    run_pkt->user_spelling = te.user->login;
  }
  /* run_pkt->user_spelling is allowed to be NULL */

  if (prob->spelling[0]) {
    run_pkt->prob_spelling = prob->spelling;
  }
  if (!run_pkt->prob_spelling) {
    run_pkt->prob_spelling = prob->short_name;
  }
  /* run_pkt->prob_spelling is allowed to be NULL */

  /* generate external representation of the packet */
  if (run_request_packet_write(run_pkt, &run_pkt_out_size, &run_pkt_out) < 0) {
    fprintf(errf, "run_request_packet_write failed\n");
    return -1;
  }

  if (generic_write_file(run_pkt_out, run_pkt_out_size, SAFE,
                         state->global->run_queue_dir, pkt_base, "") < 0) {
    fprintf(errf, "failed to write run packet\n");
    return -1;
  }

  /* update status */
  if (run_change_status(state->runlog_state, run_id, RUN_RUNNING, 0, -1,
                        judge_id) < 0) {
    return -1;
  }

  return 0;
}

void
serve_send_clar_notify_email(serve_state_t state,
                             const struct contest_desc *cnts,
                             int user_id, const unsigned char *user_name,
                             const unsigned char *subject,
                             const unsigned char *text)
{
  unsigned char esubj[1024];
  FILE *fmsg = 0;
  char *ftxt = 0;
  size_t flen = 0;
  const unsigned char *originator = 0;
  const unsigned char *mail_args[7];

  if (!state || !cnts || !cnts->clar_notify_email) return;

  snprintf(esubj, sizeof(esubj), "New clar request in contest %d", cnts->id);
  originator = serve_get_email_sender(cnts);
  fmsg = open_memstream(&ftxt, &flen);
  fprintf(fmsg, "Hello,\n\nNew clarification request is received\n"
          "Contest: %d (%s)\n"
          "User: %d (%s)\n"
          "Subject: %s\n\n"
          "%s\n\n-\n"
          "Regards,\n"
          "the ejudge contest management system\n",
          cnts->id, cnts->name, user_id, user_name, subject, text);
  fclose(fmsg); fmsg = 0;
  mail_args[0] = "mail";
  mail_args[1] = "";
  mail_args[2] = esubj;
  mail_args[3] = originator;
  mail_args[4] = cnts->clar_notify_email;
  mail_args[5] = ftxt;
  mail_args[6] = 0;
  send_job_packet(NULL, (unsigned char**) mail_args);
  xfree(ftxt); ftxt = 0;
}

void
serve_send_check_failed_email(const struct contest_desc *cnts, int run_id)
{
  unsigned char esubj[1024];
  const unsigned char *originator = 0;
  FILE *fmsg = 0;
  char *ftxt = 0;
  size_t flen = 0;
  const unsigned char *mail_args[7];

  if (!cnts->cf_notify_email) return;

  snprintf(esubj, sizeof(esubj), "Check failed in contest %d", cnts->id);
  originator = serve_get_email_sender(cnts);

  fmsg = open_memstream(&ftxt, &flen);
  fprintf(fmsg, "Hello,\n\nRun evaluation got \"Check failed\"!\n"
          "Contest: %d (%s)\n"
          "Run Id: %d\n\n-\n"
          "Regards,\n"
          "the ejudge contest management system\n",
          cnts->id, cnts->name, run_id);
  fclose(fmsg); fmsg = 0;
  mail_args[0] = "mail";
  mail_args[1] = "";
  mail_args[2] = esubj;
  mail_args[3] = originator;
  mail_args[4] = cnts->cf_notify_email;
  mail_args[5] = ftxt;
  mail_args[6] = 0;
  send_job_packet(NULL, (unsigned char **) mail_args);
  xfree(ftxt); ftxt = 0;
}

int
serve_read_compile_packet(serve_state_t state,
                          const struct contest_desc *cnts,
                          const unsigned char *compile_status_dir,
                          const unsigned char *compile_report_dir,
                          const unsigned char *pname)
{
  unsigned char rep_path[PATH_MAX];
  int  r, rep_flags = 0;
  struct run_entry re;

  char *comp_pkt_buf = 0;       /* need char* for generic_read_file */
  size_t comp_pkt_size = 0;
  struct compile_reply_packet *comp_pkt = 0;
  long report_size = 0;
  unsigned char errmsg[1024] = { 0 };
  unsigned char *team_name = 0;
  struct compile_run_extra *comp_extra = 0;
  struct section_problem_data *prob = 0;
  struct section_language_data *lang = 0;

  if ((r = generic_read_file(&comp_pkt_buf, 0, &comp_pkt_size, SAFE | REMOVE,
                             compile_status_dir, pname, "")) <= 0)
    return r;

  if (compile_reply_packet_read(comp_pkt_size, comp_pkt_buf, &comp_pkt) < 0) {
    /* failed to parse a compile packet */
    /* we can't do any reasonable recovery, just drop the packet */
    goto non_fatal_error;
  }
  if (comp_pkt->contest_id != cnts->id) {
    err("read_compile_packet: mismatched contest_id %d", comp_pkt->contest_id);
    goto non_fatal_error;
  }
  if (run_get_entry(state->runlog_state, comp_pkt->run_id, &re) < 0) {
    err("read_compile_packet: invalid run_id %d", comp_pkt->run_id);
    goto non_fatal_error;
  }
  if (comp_pkt->judge_id != re.judge_id) {
    err("read_compile_packet: judge_id mismatch: %d, %d", comp_pkt->judge_id,
        re.judge_id);
    goto non_fatal_error;
  }
  if (re.status != RUN_COMPILING) {
    err("read_compile_packet: run %d is not compiling", comp_pkt->run_id);
    goto non_fatal_error;
  }

  if (comp_pkt->status == RUN_CHECK_FAILED
      || comp_pkt->status == RUN_COMPILE_ERR) {
    if ((report_size = generic_file_size(compile_report_dir, pname, "")) < 0) {
      err("read_compile_packet: cannot get report file size");
      snprintf(errmsg, sizeof(errmsg), "cannot get size of %s/%s\n",
               compile_report_dir, pname);
      goto report_check_failed;
    }

    rep_flags = archive_make_write_path(state, rep_path, sizeof(rep_path),
                                        state->global->xml_report_archive_dir,
                                        comp_pkt->run_id, report_size, 0);
    if (rep_flags < 0) {
      snprintf(errmsg, sizeof(errmsg),
               "archive_make_write_path: %s, %d, %ld failed\n",
               state->global->xml_report_archive_dir, comp_pkt->run_id,
               report_size);
      goto report_check_failed;
    }
  }

  if (comp_pkt->status == RUN_CHECK_FAILED) {
    /* if status change fails, we cannot do reasonable recovery */
    if (run_change_status(state->runlog_state, comp_pkt->run_id,
                          RUN_CHECK_FAILED, 0, -1, 0) < 0)
      goto non_fatal_error;
    if (archive_dir_prepare(state, state->global->xml_report_archive_dir,
                            comp_pkt->run_id, 0, 0) < 0)
      goto non_fatal_error;
    if (generic_copy_file(REMOVE, compile_report_dir, pname, "",
                          rep_flags, 0, rep_path, "") < 0) {
      snprintf(errmsg, sizeof(errmsg),
               "generic_copy_file: %s, %s, %d, %s failed\n",
               compile_report_dir, pname, rep_flags, rep_path);
      goto report_check_failed;
    }
    serve_send_check_failed_email(cnts, comp_pkt->run_id);
    goto success;
  }

  if (comp_pkt->status == RUN_COMPILE_ERR) {
    /* if status change fails, we cannot do reasonable recovery */
    if (run_change_status(state->runlog_state, comp_pkt->run_id,
                          RUN_COMPILE_ERR, 0, -1, 0) < 0)
      goto non_fatal_error;

    if (archive_dir_prepare(state, state->global->xml_report_archive_dir,
                            comp_pkt->run_id, 0, 0) < 0) {
      snprintf(errmsg, sizeof(errmsg), "archive_dir_prepare: %s, %d failed\n",
               state->global->xml_report_archive_dir, comp_pkt->run_id);
      goto report_check_failed;
    }
    if (generic_copy_file(REMOVE, compile_report_dir, pname, "",
                          rep_flags, 0, rep_path, "") < 0) {
      snprintf(errmsg, sizeof(errmsg), "generic_copy_file: %s, %s, %d, %s failed\n",
               compile_report_dir, pname, rep_flags, rep_path);
      goto report_check_failed;
    }
    serve_update_standings_file(state, cnts, 0);
    goto success;
  }

  /* check run parameters */
  if (re.prob_id < 1 || re.prob_id > state->max_prob
      || !(prob = state->probs[re.prob_id])) {
    snprintf(errmsg, sizeof(errmsg), "invalid problem %d\n", re.prob_id);
    goto report_check_failed;
  }
  if (re.lang_id < 1 || re.lang_id > state->max_lang
      || !(lang = state->langs[re.lang_id])) {
    snprintf(errmsg, sizeof(errmsg), "invalid language %d\n", re.lang_id);
    goto report_check_failed;
  }
  if (!(team_name = teamdb_get_name(state->teamdb_state, re.user_id))) {
    snprintf(errmsg, sizeof(errmsg), "invalid team %d\n", re.user_id);
    goto report_check_failed;
  }
  if (prob->disable_testing && prob->enable_compilation > 0) {
    if (run_change_status(state->runlog_state, comp_pkt->run_id, RUN_ACCEPTED,
                          0, -1, comp_pkt->judge_id) < 0)
      goto non_fatal_error;
    goto success;
  }

  comp_extra = (typeof(comp_extra)) comp_pkt->run_block;
  if (!comp_extra || comp_pkt->run_block_len != sizeof(*comp_extra)
      || comp_extra->accepting_mode < 0 || comp_extra->accepting_mode > 1) {
    snprintf(errmsg, sizeof(errmsg), "invalid run block\n");
    goto report_check_failed;
  }

  if (run_change_status(state->runlog_state, comp_pkt->run_id, RUN_COMPILED,
                        0, -1, comp_pkt->judge_id) < 0)
    goto non_fatal_error;

  /*
   * so far compilation is successful, and now we prepare a run packet
   */

  if (serve_run_request(state, stderr, 0, 0, comp_pkt->run_id, re.user_id,
                        re.prob_id, re.lang_id, 0,
                        comp_extra->priority_adjustment,
                        comp_pkt->judge_id, comp_extra->accepting_mode,
                        compile_report_dir, comp_pkt) < 0) {
    snprintf(errmsg, sizeof(errmsg), "failed to write run packet\n");
    goto report_check_failed;
  }

 success:
  xfree(comp_pkt_buf);
  compile_reply_packet_free(comp_pkt);
  return 1;

 report_check_failed:
  serve_send_check_failed_email(cnts, comp_pkt->run_id);

  /* this is error recover, so if error happens again, we cannot do anything */
  if (run_change_status(state->runlog_state, comp_pkt->run_id,
                        RUN_CHECK_FAILED, 0, -1, 0) < 0)
    goto non_fatal_error;
  report_size = strlen(errmsg);
  rep_flags = archive_make_write_path(state, rep_path, sizeof(rep_path),
                                      state->global->xml_report_archive_dir,
                                      comp_pkt->run_id, report_size, 0);
  if (archive_dir_prepare(state, state->global->xml_report_archive_dir,
                          comp_pkt->run_id, 0, 0) < 0)
    goto non_fatal_error;
  /* error code is ignored */
  generic_write_file(errmsg, report_size, rep_flags, 0, rep_path, 0);
  /* goto non_fatal_error; */

 non_fatal_error:
  xfree(comp_pkt_buf);
  compile_reply_packet_free(comp_pkt);
  return 0;
}

int
serve_is_valid_status(serve_state_t state, int status, int mode)
{
  if (state->global->score_system_val == SCORE_OLYMPIAD) {
    switch (status) {
    case RUN_OK:
    case RUN_PARTIAL:
    case RUN_RUN_TIME_ERR:
    case RUN_TIME_LIMIT_ERR:
    case RUN_PRESENTATION_ERR:
    case RUN_WRONG_ANSWER_ERR:
    case RUN_ACCEPTED:
    case RUN_CHECK_FAILED:
    case RUN_MEM_LIMIT_ERR:
    case RUN_SECURITY_ERR:
      return 1;
    case RUN_COMPILE_ERR:
    case RUN_FULL_REJUDGE:
    case RUN_REJUDGE:
    case RUN_IGNORED:
    case RUN_DISQUALIFIED:
    case RUN_PENDING:
      if (mode != 1) return 0;
      return 1;
    default:
      return 0;
    }
  } else if (state->global->score_system_val == SCORE_KIROV) {
    switch (status) {
    case RUN_OK:
    case RUN_PARTIAL:
    case RUN_CHECK_FAILED:
      return 1;
    case RUN_COMPILE_ERR:
    case RUN_REJUDGE:
    case RUN_IGNORED:
    case RUN_DISQUALIFIED:
    case RUN_PENDING:
      if (mode != 1) return 0;
      return 1;
    default:
      return 0;
    }
  } else {
    switch (status) {
    case RUN_OK:
    case RUN_RUN_TIME_ERR:
    case RUN_TIME_LIMIT_ERR:
    case RUN_PRESENTATION_ERR:
    case RUN_WRONG_ANSWER_ERR:
    case RUN_CHECK_FAILED:
    case RUN_MEM_LIMIT_ERR:
    case RUN_SECURITY_ERR:
      return 1;
    case RUN_COMPILE_ERR:
    case RUN_REJUDGE:
    case RUN_IGNORED:
    case RUN_DISQUALIFIED:
    case RUN_PENDING:
      if (mode != 1) return 0;
      return 1;
    default:
      return 0;
    }
  }
}

static unsigned char *
time_to_str(unsigned char *buf, size_t size, int secs, int usecs)
{
  struct tm *ltm;
  time_t tt = secs;

  if (secs <= 0) {
    snprintf(buf, size, "N/A");
    return buf;
  }
  ltm = localtime(&tt);
  snprintf(buf, size, "%04d/%02d/%02d %02d:%02d:%02d.%06d",
           ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
           ltm->tm_hour, ltm->tm_min, ltm->tm_sec, usecs);
  return buf;
}

static unsigned char *
dur_to_str(unsigned char *buf, size_t size, int sec1, int usec1,
           int sec2, int usec2)
{
  long long d;

  if (sec1 <= 0 || sec2 <= 0) {
    snprintf(buf, size, "N/A");
    return buf;
  }
  if ((d = sec2 * 1000000 + usec2 - (sec1 * 1000000 + usec1)) < 0) {
    snprintf(buf, size, "t1 > t2");
    return buf;
  }
  d = (d + 500) / 1000;
  snprintf(buf, size, "%lld.%03lld", d / 1000, d % 1000);
  return buf;
}

int
serve_read_run_packet(serve_state_t state,
                      const struct contest_desc *cnts,
                      const unsigned char *run_status_dir,
                      const unsigned char *run_report_dir,
                      const unsigned char *run_full_archive_dir,
                      const unsigned char *pname)
{
  path_t rep_path, full_path;
  int r, rep_flags, rep_size, full_flags;
  struct run_entry re;
  char *reply_buf = 0;          /* need char* for generic_read_file */
  size_t reply_buf_size = 0;
  struct run_reply_packet *reply_pkt = 0;
  char *audit_text = 0;
  size_t audit_text_size = 0;
  FILE *f = 0;
  int ts8, ts8_us;
  unsigned char time_buf[64];

  get_current_time(&ts8, &ts8_us);
  if ((r = generic_read_file(&reply_buf, 0, &reply_buf_size, SAFE | REMOVE,
                             run_status_dir, pname, "")) <= 0)
    return r;

  if (run_reply_packet_read(reply_buf_size, reply_buf, &reply_pkt) < 0)
    goto failed;
  xfree(reply_buf), reply_buf = 0;

  if (reply_pkt->contest_id != cnts->id) {
    err("read_run_packet: contest_id mismatch: %d in packet",
        reply_pkt->contest_id);
    goto failed;
  }
  if (run_get_entry(state->runlog_state, reply_pkt->run_id, &re) < 0) {
    err("read_run_packet: invalid run_id: %d", reply_pkt->run_id);
    goto failed;
  }
  if (re.status != RUN_RUNNING) {
    err("read_run_packet: run %d status is not RUNNING", reply_pkt->run_id);
    goto failed;
  }
  if (re.judge_id != reply_pkt->judge_id) {
    err("read_run_packet: judge_id mismatch: packet: %d, db: %d",
        reply_pkt->judge_id, re.judge_id);
    goto failed;
  }

  if (!serve_is_valid_status(state, reply_pkt->status, 2))
    goto bad_packet_error;

  if (state->global->score_system_val == SCORE_OLYMPIAD) {
    if (re.prob_id < 1 || re.prob_id > state->max_prob
        || !state->probs[re.prob_id])
      goto bad_packet_error;
  } else if (state->global->score_system_val == SCORE_KIROV) {
    /*
    if (status != RUN_PARTIAL && status != RUN_OK
        && status != RUN_CHECK_FAILED) goto bad_packet_error;
    */
    if (re.prob_id < 1 || re.prob_id > state->max_prob
        || !state->probs[re.prob_id])
      goto bad_packet_error;
    if (reply_pkt->score < 0
        || reply_pkt->score > state->probs[re.prob_id]->full_score)
      goto bad_packet_error;
    /*
    for (n = 0; n < serve_state.probs[re.prob_id]->dp_total; n++)
      if (re.timestamp < serve_state.probs[re.prob_id]->dp_infos[n].deadline)
        break;
    if (n < serve_state.probs[re.prob_id]->dp_total) {
      score += serve_state.probs[re.prob_id]->dp_infos[n].penalty;
      if (score > serve_state.probs[re.prob_id]->full_score)
        score = serve_state.probs[re.prob_id]->full_score;
      if (score < 0) score = 0;
    }
    */
  } else if (state->global->score_system_val == SCORE_MOSCOW) {
    if (re.prob_id < 1 || re.prob_id > state->max_prob
        || !state->probs[re.prob_id])
      goto bad_packet_error;
    if (reply_pkt->score < 0
        || reply_pkt->score > state->probs[re.prob_id]->full_score)
      goto bad_packet_error;
  } else {
    reply_pkt->score = -1;
  }
  if (reply_pkt->status == RUN_CHECK_FAILED)
    serve_send_check_failed_email(cnts, reply_pkt->run_id);
  if (run_change_status(state->runlog_state, reply_pkt->run_id,
                        reply_pkt->status, reply_pkt->failed_test,
                        reply_pkt->score, 0) < 0) return -1;
  serve_update_standings_file(state, cnts, 0);
  rep_size = generic_file_size(run_report_dir, pname, "");
  if (rep_size < 0) return -1;
  rep_flags = archive_make_write_path(state, rep_path, sizeof(rep_path),
                                      state->global->xml_report_archive_dir,
                                      reply_pkt->run_id, rep_size, 0);
  if (archive_dir_prepare(state, state->global->xml_report_archive_dir,
                          reply_pkt->run_id, 0, 0) < 0)
    return -1;
  if (generic_copy_file(REMOVE, run_report_dir, pname, "",
                        rep_flags, 0, rep_path, "") < 0)
    return -1;
  /*
  if (serve_state.global->team_enable_rep_view) {
    team_size = generic_file_size(run_team_report_dir, pname, "");
    team_flags = archive_make_write_path(team_path, sizeof(team_path),
                                         serve_state.global->team_report_archive_dir,
                                         reply_pkt->run_id, team_size, 0);
    if (archive_dir_prepare(serve_state.global->team_report_archive_dir,
                            reply_pkt->run_id, 0, 0) < 0)
      return -1;
    if (generic_copy_file(REMOVE, run_team_report_dir, pname, "",
                          team_flags, 0, team_path, "") < 0)
      return -1;
  }
  */
  if (state->global->enable_full_archive) {
    full_flags = archive_make_write_path(state, full_path, sizeof(full_path),
                                         state->global->full_archive_dir,
                                         reply_pkt->run_id, 0, 0);
    if (archive_dir_prepare(state, state->global->full_archive_dir,
                            reply_pkt->run_id, 0, 0) < 0)
      return -1;
    if (generic_copy_file(REMOVE, run_full_archive_dir, pname, "",
                          0, 0, full_path, "") < 0)
      return -1;
  }

  /* add auditing information */
  if (!(f = open_memstream(&audit_text, &audit_text_size))) return 1;
  fprintf(f, "Status: Judging complete\n");
  fprintf(f, "  Profiling information:\n");
  fprintf(f, "  Request start time:                %s\n",
          time_to_str(time_buf, sizeof(time_buf),
                      reply_pkt->ts1, reply_pkt->ts1_us));
  fprintf(f, "  Request completion time:           %s\n",
          time_to_str(time_buf, sizeof(time_buf),
                      ts8, ts8_us));
  fprintf(f, "  Total testing duration:            %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts1, reply_pkt->ts1_us,
                     ts8, ts8_us));
  fprintf(f, "  Waiting in compile queue duration: %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts1, reply_pkt->ts1_us,
                     reply_pkt->ts2, reply_pkt->ts2_us));
  fprintf(f, "  Compilation duration:              %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts2, reply_pkt->ts2_us,
                     reply_pkt->ts3, reply_pkt->ts3_us));
  fprintf(f, "  Waiting in serve queue duration:   %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts3, reply_pkt->ts3_us,
                     reply_pkt->ts4, reply_pkt->ts4_us));
  fprintf(f, "  Waiting in run queue duration:     %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts4, reply_pkt->ts4_us,
                     reply_pkt->ts5, reply_pkt->ts5_us));
  fprintf(f, "  Testing duration:                  %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts5, reply_pkt->ts5_us,
                     reply_pkt->ts6, reply_pkt->ts6_us));
  fprintf(f, "  Post-processing duration:          %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts6, reply_pkt->ts6_us,
                     reply_pkt->ts7, reply_pkt->ts7_us));
  fprintf(f, "  Waiting in serve queue duration:   %s\n",
          dur_to_str(time_buf, sizeof(time_buf),
                     reply_pkt->ts7, reply_pkt->ts7_us,
                     ts8, ts8_us));
  fprintf(f, "\n");
  fclose(f);
  serve_audit_log(state, reply_pkt->run_id, 0, 0, 0, "%s", audit_text);

  return 1;

 bad_packet_error:
  err("bad_packet");

 failed:
  xfree(reply_buf);
  run_reply_packet_free(reply_pkt);
  return 0;
}

void
serve_rejudge_run(serve_state_t state,
                  int run_id,
                  int user_id, ej_ip_t ip, int ssl_flag,
                  int force_full_rejudge,
                  int priority_adjustment)
{
  struct run_entry re;
  int accepting_mode = -1;

  if (run_get_entry(state->runlog_state, run_id, &re) < 0) return;
  if (re.is_imported) return;
  if (re.is_readonly) return;
 
  if (re.prob_id <= 0 || re.prob_id > state->max_prob
      || !state->probs[re.prob_id]) {
    err("rejudge_run: bad problem: %d", re.prob_id);
    return;
  }
  if (state->probs[re.prob_id]->type_val > 0) {
    if (force_full_rejudge
        && state->global->score_system_val == SCORE_OLYMPIAD) {
      accepting_mode = 0;
    }
    // FIXME: handle output-only problems
    serve_audit_log(state, run_id, user_id, ip, ssl_flag, "Command: Rejudge\n");
    return;
  }

  if (re.lang_id <= 0 || re.lang_id > state->max_lang
      || !state->langs[re.lang_id]) {
    err("rejudge_run: bad language: %d", re.lang_id);
    return;
  }

  if (force_full_rejudge && state->global->score_system_val == SCORE_OLYMPIAD) {
    accepting_mode = 0;
  }

  serve_compile_request(state, 0, -1, run_id,
                        state->langs[re.lang_id]->compile_id, re.locale_id,
                        (state->probs[re.prob_id]->type_val > 0),
                        state->langs[re.lang_id]->src_sfx,
                        state->langs[re.lang_id]->compiler_env,
                        accepting_mode, priority_adjustment);

  serve_audit_log(state, run_id, user_id, ip, ssl_flag, "Command: Rejudge\n");
}

/*
 * Local variables:
 *  compile-command: "make"
 *  c-font-lock-extra-types: ("\\sw+_t" "FILE")
 * End:
 */
