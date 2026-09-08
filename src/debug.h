// Debug log: timestamped events kept in a ring for the on-screen overlay, appended to hollow.log,
// and dumped as a text snapshot to the clipboard with F8 for pasting into a bug report.
#pragma once
#include <stdbool.h>

void dbg_init(const char *log_path);
void dbg_set_time(double t);
void dbg_log(const char *fmt, ...);
int  dbg_line_count(void);
const char *dbg_line(int i);          // 0 = oldest kept line
// Compose a snapshot (header text supplied by the game) plus the recent log, write it to
// hollow_snapshot.txt and put it on the clipboard. Returns false if either failed.
bool dbg_snapshot(const char *header);
void dbg_shutdown(void);
