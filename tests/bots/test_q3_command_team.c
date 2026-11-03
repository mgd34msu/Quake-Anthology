/*
 * C checks for src/bots/behavior/q3/ai-{orders,team,chat,command,voice,input,main}.ts and
 * arsenal-knowledge.ts. tests/bots has no TS test importing these modules
 * directly, so the expected transcript below was produced by running the TS
 * modules under bun on the same fixture (scratch harness oracle.ts: one shared
 * real BotLibrary/GameAiContext/BotState with fake game, host and navigation).
 * Every host callback checks that it receives the fixture as its ctx.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_input.h"
#include "qa/bots/behavior/q3/ai_main.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/bots/behavior/q3/ai_voice.h"
#include "qa/bots/behavior/q3/arsenal_knowledge.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
    }                                                                                              \
  } while (0)
#define CHECK_ABORTS(...)                                                                          \
  do {                                                                                             \
    checks++;                                                                                      \
    fflush(NULL);                                                                                  \
    pid_t pid = fork();                                                                            \
    if (pid == 0) {                                                                                \
      (void)prctl(PR_SET_DUMPABLE, 0);                                                             \
      if (freopen("/dev/null", "w", stderr) == NULL)                                               \
        _exit(2);                                                                                  \
      __VA_ARGS__;                                                                                 \
      _exit(0);                                                                                    \
    }                                                                                              \
    int status = 0;                                                                                \
    waitpid(pid, &status, 0);                                                                      \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                                   \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__);       \
    }                                                                                              \
  } while (0)

static const char *const expected[] = {
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "name 0 [Sarge] [Sar] [sarge] []",
    "name 1 [Mr [TAG]Anarki] [Mr ] [anarki] []",
    "name 3 [HumanBob] [Hum] [humanbob] []",
    "name 7 [] [] [] []",
    "print \"^1Error: ClientName: client out of range\\n\"",
    "print \"^1Error: ClientName: client out of range\\n\"",
    "print \"^1Error: ClientName: client out of range\\n\"",
    "print \"^1Error: ClientSkin: client out of range\\n\"",
    "name 64 [[client out of range]] [[client out of range]] [] [[client out of range]]",
    "print \"^1Error: ClientName: client out of range\\n\"",
    "print \"^1Error: ClientName: client out of range\\n\"",
    "print \"^1Error: ClientName: client out of range\\n\"",
    "print \"^1Error: ClientSkin: client out of range\\n\"",
    "name -1 [[client out of range]] [[client out of range]] [] [[client out of range]]",
    "from sarge 0 0",
    "from MR [TAG]ANARKI 1 1",
    "from humanbob 3 3",
    "from nobody -1 -1",
    "team 1 2 1 0 0 1031",
    "stristr World null abc null",
    "clean [Red^ ok^] [Red^ ok^]",
    "userinfo 0 \\teamtask\\4",
    "changed 0",
    "userinfo 0 \\teamtask\\4",
    "changed 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "seek 3 700 0 0 0 101.509567 0 [] [] 0 0 0 0 0 105 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 3 700 0 0 0 101.509567 0 [] [] 0 0 1 0 0 105 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 1 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 1 1 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "retreat 5 320 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "carry 5 320 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "refuse 5 320 0 1 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 6 0 0 0",
    "set 0",
    "set 5 320 0 1 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "userinfo 0 \\teamtask\\2",
    "changed 0",
    "seek 3 700 0 0 0 101.509567 0 [] [] 0 0 0 0 0 105 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 3 700 0 0 0 101.509567 0 [] [] 0 0 1 0 0 105 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "userinfo 0 \\teamtask\\5",
    "changed 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 1 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 1 1 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "seek 6 720 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "userinfo 0 \\teamtask\\2",
    "changed 0",
    "retreat 5 320 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "carry 5 320 0 0 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 vtell 0 no",
    "refuse 5 320 0 1 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 6 0 0 0",
    "set 0",
    "set 5 320 0 1 0 0 0 [] [] 0 0 0 0 0 125 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "teamai 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 112.547836 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team iamteamleader",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 140 5 0 0 0 0 0 0 0",
    "pref 1 0 5",
    "cmd 0 tell 5 cmd_defendbase 0=Xaero",
    "cmd 0 tell 1 cmd_defendbase 0=Mr [TAG]Anarki",
    "cmd 0 tell 3 cmd_getflag 0=HumanBob",
    "cmd 0 tell 6 cmd_getflag 0=Hunter",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 1 0 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 tell 5 cmd_defendbase 0=Xaero",
    "cmd 0 tell 3 cmd_getflag 0=HumanBob",
    "cmd 0 tell 6 cmd_getflag 0=Hunter",
    "cmd 0 tell 1 cmd_getflag 0=Mr [TAG]Anarki",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 0 1 260 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 tell 5 cmd_defendbase 0=Xaero",
    "cmd 0 tell 3 cmd_accompany 0=HumanBob 1=Mr [TAG]Anarki",
    "cmd 0 tell 6 cmd_accompany 0=Hunter 1=Mr [TAG]Anarki",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 1 1 260 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 tell 5 cmd_accompany 0=Xaero 1=Mr [TAG]Anarki",
    "cmd 0 tell 3 cmd_getflag 0=HumanBob",
    "cmd 0 tell 6 cmd_getflag 0=Hunter",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 1 1 600 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "teamai 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 112.547836 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team iamteamleader",
    "cmd 0 vsay_team startleader",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 140 5 0 0 0 0 0 0 0",
    "pref 1 0 5",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 getflag",
    "cmd 0 vtell 6 getflag",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 1 0 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 5 getflag",
    "cmd 0 vtell 5 getflag",
    "cmd 0 vtell 5 getflag",
    "cmd 0 vtell 5 getflag",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 0 1 260 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 3 followflagcarrier",
    "cmd 0 vtell 6 followflagcarrier",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 1 1 260 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 followflagcarrier",
    "cmd 0 vtell 0 followflagcarrier",
    "cmd 0 vtell 3 returnflag",
    "cmd 0 vtell 6 returnflag",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 1 1 600 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "teamai 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 112.547836 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team iamteamleader",
    "cmd 0 vsay_team startleader",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 140 5 0 0 0 0 0 0 0",
    "pref 1 0 5",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 getflag",
    "cmd 0 vtell 6 getflag",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 1 0 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 3 followflagcarrier",
    "cmd 0 vtell 6 followflagcarrier",
    "cmd 0 vtell 0 followflagcarrier",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 0 1 260 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 6 defend",
    "cmd 0 vtell 3 getflag",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 1 1 260 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 getflag",
    "cmd 0 vtell 6 getflag",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 1 1 600 0 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "teamai 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 112.547836 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team iamteamleader",
    "cmd 0 vsay_team startleader",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 140 5 0 0 0 0 0 0 0",
    "pref 1 0 5",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 1 0 0 0 0 0 0 0 0 230 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 0 1 0 0 0 0 0 0 0 0 290 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 1 1 0 0 0 0 0 0 0 0 350 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 1 1 0 0 0 0 0 0 0 0 630 5 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "teamai 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 112.547836 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team iamteamleader",
    "cmd 0 vsay_team startleader",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 140 5 0 0 0 0 0 0 0",
    "pref 1 0 5",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 1 0 0 0 0 0 0 0 0 230 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 0 1 0 0 0 0 0 0 0 0 290 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 1 1 0 0 0 0 0 0 0 0 350 5 0 0 0 0 0 0 0",
    "cmd 0 vtell 5 defend",
    "cmd 0 vtell 0 defend",
    "cmd 0 vtell 1 defend",
    "cmd 0 vtell 3 offense",
    "cmd 0 vtell 6 offense",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 1 1 0 0 0 0 0 0 0 0 630 5 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "teamai 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 112.547836 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team iamteamleader",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "teamai 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 0 0 0 0 0 0 0 0 0 140 5 0 0 0 0 0 0 0",
    "pref 1 0 5",
    "cmd 0 tell 1 cmd_accompanyme 0=Mr [TAG]Anarki",
    "cmd 0 tell 5 cmd_accompany 0=Xaero 1=HumanBob",
    "cmd 0 tell 6 cmd_accompany 0=Hunter 1=HumanBob",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 0 1 0 0 0 0 0 0 0 0 320 5 0 0 0 0 0 0 0",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 0 1 0 0 0 0 0 0 0 0 320 5 0 0 0 0 0 0 0",
    "cmd 0 tell 1 cmd_accompanyme 0=Mr [TAG]Anarki",
    "cmd 0 tell 5 cmd_accompany 0=Xaero 1=HumanBob",
    "cmd 0 tell 6 cmd_accompany 0=Hunter 1=HumanBob",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 0 1 1 0 0 0 0 0 0 0 0 450 5 0 0 0 0 0 0 0",
    "cmd 0 tell 1 cmd_accompanyme 0=Mr [TAG]Anarki",
    "cmd 0 tell 5 cmd_accompany 0=Xaero 1=HumanBob",
    "cmd 0 tell 6 cmd_accompany 0=Hunter 1=HumanBob",
    "orders 0 0 0 0 0 0 0 [Sarge] [] 0 1 1 1 0 0 0 0 0 0 0 0 720 5 0 0 0 0 0 0 0",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "voice 0 1 0 getflag 1",
    "v 4 700 1 1 100 101.509567 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "voice 0 1 0 offense 1",
    "v 4 700 1 1 100 101.139503 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "voice 0 1 0 DEFEND 1",
    "v 3 700 1 1 100 101.731133 0 [] [] 0 0 0 0 0 0 0 7 0 0 0 0 0 0 0 0 0 3 0 0 0",
    "cmd 0 tell 1 dismissed",
    "voice 0 1 0 patrol 1",
    "v 0 700 1 1 100 101.731133 0 [] [] 0 0 0 0 0 0 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "voice 0 1 0 camp 1",
    "v 8 700 1 1 100 101.902588 1 [] [] 0 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 8 0 0 0",
    "voice 0 1 0 followme 1",
    "v 2 700 1 1 100 101.627495 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 1 0 returnflag 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 3 0 startleader 1",
    "v 6 280 1 1 100 101.750603 1 [HumanBob] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 3 0 whoisleader 1",
    "v 6 280 1 1 100 101.750603 1 [HumanBob] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 3 0 stopleader 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "cmd 0 tell 1 keepinmind 0=anarki",
    "voice 0 1 0 wantondefense 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "cmd 0 tell 1 keepinmind 0=anarki",
    "voice 0 1 0 wantonoffense 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 2 0 getflag 0",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 1 0 bogus 0",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice0 0",
    "match Mr [TAG]Anarki help me 1",
    "m 1 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "match Mr [TAG]Anarki help Klesk 1",
    "m 1 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "match Human^1Bob help Mr [TAG]Anarki 1",
    "m 1 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "cmd 0 tell 1 dismissed",
    "match Mr [TAG]Anarki dismiss 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team noteam",
    "match Mr [TAG]Anarki which team 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 joinedteam 0=alpha",
    "match Mr [TAG]Anarki join alpha 1",
    "m 0 161 1 1 101 101.581985 1 [] [alpha] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team inteam 0=alpha",
    "match Mr [TAG]Anarki which team 1",
    "m 0 161 1 1 101 101.581985 1 [] [alpha] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 leftteam 0=alpha",
    "match Mr [TAG]Anarki leave team 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Sarge i lead 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team I'm the team leader",
    "",
    "match Mr [TAG]Anarki who leads 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 keepinmind 0=anarki",
    "match Mr [TAG]Anarki defender 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki stop leading 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Sarge stop leading 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki kill Doom 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 112 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 whois 0=Nobody",
    "match Mr [TAG]Anarki kill Nobody 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 112 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki feet 10 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 97.536003 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki space 2 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 64 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki space 99 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 killing 0=Doom",
    "match Mr [TAG]Anarki doing 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "print \"^3Warning: BotCreateWayPoint: Out of waypoints\\n\"",
    "match Mr [TAG]Anarki checkpoint base at 1 2 3 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 checkpoint_invalid",
    "match Mr [TAG]Anarki checkpoint base at 4 5 -9 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "print \"^3Warning: BotCreateWayPoint: Out of waypoints\\n\"",
    "match Mr [TAG]Anarki checkpoint base at 7 8 9 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki get flag 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Klesk got the red flag 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 1 0 0 124 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Mr [TAG]Anarki got the blue flag 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 1 1 1 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Klesk captured 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "cmd 0 say_team the part of my brain to create formations has been damaged",
    "match Mr [TAG]Anarki formation 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "print \"unknown match type\\n\"",
    "match Mr [TAG]Anarki weird 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Sarge my command 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match nothing matches 0",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "print \"-12\\n\"",
    "print \"34\\n\"",
    "print \"56\\n\"",
    "gps {\"x\":-12,\"y\":34,\"z\":56}",
    "find 2 1 4 2",
    "print \"Sarge: I'm gonna defend a key area for 593 secs\\n\"",
    "print \"Sarge: I'm gonna roam for 170 secs\\n\"",
    "print \"Sarge: I've got a regular goal\\n\"",
    "leader 0",
    "print \"Sarge                  : roaming\\n\"",
    "print \"Sarge                ^1F : capturing flag\\n\"",
    "cs 25 l\\ \\c\\F \\a\\capturing flag",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "userinfo 0 \\teamtask\\1",
    "changed 0",
    "voice 0 1 0 getflag 1",
    "v 4 700 1 1 100 101.509567 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "userinfo 0 \\teamtask\\1",
    "changed 0",
    "voice 0 1 0 offense 1",
    "v 4 700 1 1 100 101.139503 0 [] [] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "userinfo 0 \\teamtask\\2",
    "changed 0",
    "voice 0 1 0 DEFEND 1",
    "v 3 700 1 1 100 101.731133 0 [] [] 0 0 0 0 0 0 0 7 0 0 0 0 0 0 0 0 0 3 0 0 0",
    "cmd 0 tell 1 dismissed",
    "cmd 0 vosay_team onpatrol",
    "userinfo 0 \\teamtask\\3",
    "changed 0",
    "voice 0 1 0 patrol 1",
    "v 0 700 1 1 100 101.731133 0 [] [] 0 0 0 0 0 0 0 7 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "userinfo 0 \\teamtask\\7",
    "changed 0",
    "voice 0 1 0 camp 1",
    "v 8 700 1 1 100 101.902588 1 [] [] 0 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 8 0 0 0",
    "userinfo 0 \\teamtask\\4",
    "changed 0",
    "voice 0 1 0 followme 1",
    "v 2 700 1 1 100 101.627495 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "userinfo 0 \\teamtask\\5",
    "changed 0",
    "voice 0 1 0 returnflag 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 3 0 startleader 1",
    "v 6 280 1 1 100 101.750603 1 [HumanBob] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 3 0 whoisleader 1",
    "v 6 280 1 1 100 101.750603 1 [HumanBob] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 3 0 stopleader 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "cmd 0 tell 1 keepinmind 0=anarki",
    "cmd 0 votell 1 yes",
    "voice 0 1 0 wantondefense 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "cmd 0 tell 1 keepinmind 0=anarki",
    "cmd 0 votell 1 yes",
    "voice 0 1 0 wantonoffense 1",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 2 0 getflag 0",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice 0 1 0 bogus 0",
    "v 6 280 1 1 100 101.750603 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "voice0 0",
    "match Mr [TAG]Anarki help me 1",
    "m 1 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "match Mr [TAG]Anarki help Klesk 1",
    "m 1 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "match Human^1Bob help Mr [TAG]Anarki 1",
    "m 1 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 2 0 0 0",
    "cmd 0 tell 1 dismissed",
    "match Mr [TAG]Anarki dismiss 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team noteam",
    "match Mr [TAG]Anarki which team 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 joinedteam 0=alpha",
    "match Mr [TAG]Anarki join alpha 1",
    "m 0 161 1 1 101 101.581985 1 [] [alpha] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team inteam 0=alpha",
    "match Mr [TAG]Anarki which team 1",
    "m 0 161 1 1 101 101.581985 1 [] [alpha] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 leftteam 0=alpha",
    "match Mr [TAG]Anarki leave team 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Sarge i lead 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 say_team I'm the team leader",
    "",
    "match Mr [TAG]Anarki who leads 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 keepinmind 0=anarki",
    "cmd 0 votell 1 yes",
    "match Mr [TAG]Anarki defender 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki stop leading 1",
    "m 0 161 1 1 101 101.581985 1 [Sarge] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Sarge stop leading 1",
    "m 0 161 1 1 101 101.581985 1 [] [] 112 0 0 0 0 0 1 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "userinfo 0 \\teamtask\\3",
    "changed 0",
    "match Mr [TAG]Anarki kill Doom 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 112 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 whois 0=Nobody",
    "match Mr [TAG]Anarki kill Nobody 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 112 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki feet 10 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 97.536003 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki space 2 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 64 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "match Mr [TAG]Anarki space 99 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 killing 0=Doom",
    "match Mr [TAG]Anarki doing 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "print \"^3Warning: BotCreateWayPoint: Out of waypoints\\n\"",
    "match Mr [TAG]Anarki checkpoint base at 1 2 3 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "cmd 0 tell 1 checkpoint_invalid",
    "match Mr [TAG]Anarki checkpoint base at 4 5 -9 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "print \"^3Warning: BotCreateWayPoint: Out of waypoints\\n\"",
    "match Mr [TAG]Anarki checkpoint base at 7 8 9 1",
    "m 11 294 1 1 101 115.022919 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "userinfo 0 \\teamtask\\1",
    "changed 0",
    "match Mr [TAG]Anarki get flag 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Klesk got the red flag 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 1 0 0 124 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Mr [TAG]Anarki got the blue flag 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 1 1 1 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Klesk captured 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "cmd 0 say_team the part of my brain to create formations has been damaged",
    "match Mr [TAG]Anarki formation 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "print \"unknown match type\\n\"",
    "match Mr [TAG]Anarki weird 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match Sarge my command 1",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "match nothing matches 0",
    "m 4 723 1 1 123 123.823204 1 [] [] 100 0 0 0 125 0 4 4 0 0 0 0 0 0 0 0 0 4 0 0 0",
    "print \"-12\\n\"",
    "print \"34\\n\"",
    "print \"56\\n\"",
    "gps {\"x\":-12,\"y\":34,\"z\":56}",
    "find 2 1 4 2",
    "print \"Sarge: I'm gonna defend a key area for 593 secs\\n\"",
    "print \"Sarge: I'm gonna roam for 170 secs\\n\"",
    "print \"Sarge: I've got a regular goal\\n\"",
    "leader 0",
    "print \"Sarge                  : roaming\\n\"",
    "print \"Sarge                ^1F : capturing flag\\n\"",
    "cs 25 l\\ \\c\\F \\a\\capturing flag",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "active 5 0 1 anarki klesk",
    "valid 1 0 [q3dm17]",
    "weapon 0 [unknown weapon]",
    "weapon 1 Shotgun",
    "weapon 2 Gauntlet",
    "weapon 3 Machinegun",
    "weapon 4 Grenade Launcher",
    "weapon 5 Grenade Launcher",
    "weapon 6 Rocket Launcher",
    "weapon 7 Rocket Launcher",
    "weapon 8 Plasmagun",
    "weapon 9 Plasmagun",
    "weapon 10 Railgun",
    "weapon 11 Lightning Gun",
    "weapon 12 BFG10K",
    "weapon 13 BFG10K",
    "weapon 14 [unknown weapon]",
    "weapon 15 [unknown weapon]",
    "weapon 16 [unknown weapon]",
    "weapon 17 [unknown weapon]",
    "weapon 18 [unknown weapon]",
    "weapon 19 [unknown weapon]",
    "weapon 20 [unknown weapon]",
    "weapon 21 [unknown weapon]",
    "weapon 22 [unknown weapon]",
    "weapon 23 Grapple",
    "weapon 24 [unknown weapon]",
    "weapon 25 [unknown weapon]",
    "weapon 26 [unknown weapon]",
    "weapon 27 [unknown weapon]",
    "weapon 28 [unknown weapon]",
    "weapon 29 [unknown weapon]",
    "rw Railgun klesk",
    "rw Lightning Gun humanbob",
    "rw Lightning Gun humanbob",
    "rw Grenade Launcher anarki",
    "rw Gauntlet anarki",
    "rw Shotgun humanbob",
    "death 1 [death_rail 0=klesk 1=Railgun]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 100 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 [death_praise 0=klesk 1=Railgun]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 130 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 []",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 160 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 []",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 190 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 []",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 220 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 [death_insult 0=sarge 1=[unknown weapon]]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 250 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 [death_insult 0=anarki 1=Rocket Launcher]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 280 0 0 0 0 0 0 0 0 0 0 0 0",
    "kill 1 [kill_rail 0=klesk]",
    "kill 1 []",
    "kill 0 []",
    "kill 1 [kill_praise 0=klesk]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=anarki 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=humanbob 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=klesk 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=anarki 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "fast 1 [random_insult 0=humanbob 1=klesk 2=[invalid var] 3=[invalid var] 4=q3dm17 5=Lightning Gun]",
    "lib 3 characteristic 23 is not initialized",
    "chattime 2",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "active 5 0 1 anarki klesk",
    "valid 1 0 [q3dm17]",
    "weapon 0 [unknown weapon]",
    "weapon 1 Shotgun",
    "weapon 2 Gauntlet",
    "weapon 3 Machinegun",
    "weapon 4 Grenade Launcher",
    "weapon 5 Grenade Launcher",
    "weapon 6 Rocket Launcher",
    "weapon 7 Rocket Launcher",
    "weapon 8 Plasmagun",
    "weapon 9 Plasmagun",
    "weapon 10 Railgun",
    "weapon 11 Lightning Gun",
    "weapon 12 BFG10K",
    "weapon 13 BFG10K",
    "weapon 14 [unknown weapon]",
    "weapon 15 [unknown weapon]",
    "weapon 16 [unknown weapon]",
    "weapon 17 [unknown weapon]",
    "weapon 18 [unknown weapon]",
    "weapon 19 [unknown weapon]",
    "weapon 20 [unknown weapon]",
    "weapon 21 [unknown weapon]",
    "weapon 22 [unknown weapon]",
    "weapon 23 Nailgun",
    "weapon 24 Chaingun",
    "weapon 25 Proximity Launcher",
    "weapon 26 Kamikaze",
    "weapon 27 Prox mine",
    "weapon 28 Grapple",
    "weapon 29 [unknown weapon]",
    "rw Nailgun klesk",
    "rw Proximity Launcher humanbob",
    "rw Chaingun humanbob",
    "rw Rocket Launcher anarki",
    "rw Gauntlet anarki",
    "rw Shotgun humanbob",
    "death 1 [death_rail 0=klesk 1=Railgun]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 100 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 [death_praise 0=klesk 1=Railgun]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 130 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 []",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 160 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 []",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 190 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 []",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 220 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 [death_insult 0=sarge 1=Kamikaze]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 250 0 0 0 0 0 0 0 0 0 0 0 0",
    "death 1 [death_insult 0=anarki 1=Rocket Launcher]",
    "d 0 0 0 0 0 0 0 [] [] 0 0 0 0 0 0 0 0 280 0 0 0 0 0 0 0 0 0 0 0 0",
    "kill 1 [kill_rail 0=klesk]",
    "kill 1 []",
    "kill 0 []",
    "kill 1 [kill_praise 0=klesk]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=anarki 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=humanbob 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=klesk 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "misc 1 0 0 0 0 0 0 [game_enter 0=sarge 1=anarki 2=[invalid var] 3=[invalid var] 4=q3dm17]",
    "fast 1 [random_insult 0=humanbob 1=klesk 2=[invalid var] 3=[invalid var] 4=q3dm17 5=Chaingun]",
    "lib 3 characteristic 23 is not initialized",
    "chattime 2",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "ucmd 12345 820 18384 -4464 1 5 0 127 127",
    "ucmd 12345 -6461 -27037 -3918 1057 13 -38 -95 -127",
    "ucmd 12345 -1019 1982 -4464 990 0 0 -127 127",
    "add 15.490723 339.010620 25.021362",
    "sub 9.997559 349.996948 0.499878",
    "diff 20 -20 354.995728 349.996948",
    "view 7.492676 297.246094 200.000610 -1.068685 -1.175586",
    "view -3.015747 300.278320 200.000610 -4.493868 1.298114",
    "view -19.857788 308.106079 200.000610 -7.197782 3.348313",
    "view -49.998779 344.498291 200.000610 -17.774656 10.012706",
    "view -110.000610 34.771729 200.000610 -20.373665 11.312160",
    "view -170.002441 62.506714 200.000610 -14.793012 6.241153",
    "update 20000 29612 13057 0 0 0 162.663574 71.724243",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "lib 3 move state handle 0 out of range",
    "lib 3 move state handle 0 out of range",
    "lib 3 move state handle 0 out of range",
    "activation 2 1 0",
    "tactics 1 1 64 null null 0 0",
    "tactics 2 0 null 8 null 90 0",
    "tactics 5 0 null 10 17 0 1",
    "tactics 7 0 null 14 null 0 0",
    "tactics 4 0 300 11 18 0 1",
    "tactics 9 0 null null null 0 0",
    "info 5 undefined",
    "utility 152 0",
    "quad 70",
    "aggr 0",
    "update-inventory",
    "lib 1 loaded syn.c",
    "lib 1 loaded rnd.c",
    "lib 1 loaded match.c",
    "lib 1 loaded rchat.c",
    "lib 1 no rchats",
    "lib 1 loaded test from test_t.c",
    "load 1",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded skill 1 from test_c.c",
    "character 1",
    "setup 1",
    "spawn",
    "cs 25 l\\ \\c\\  \\a\\accompanying anarki",
    "usercmd 0 1000 0 0",
    "frame 1",
    "print \"^1Fatal: BotAISetupClient: client 0 already setup\\n\"",
    "client 0",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 counldn't load missing_c.c",
    "lib 2 couldn't find skill 1 in missing_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load missing_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from missing_c.c",
    "print \"^1Fatal: couldn't load skill 1.000000 from missing_c.c\\n\"",
    "client 0 failed",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 1 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 3 loaded cached skill 1.000000 from test_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't find skill 4 in bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 2 couldn't load any skill from bots/default_c.c",
    "lib 2 couldn't find skill 4 in test_c.c",
    "lib 3 counldn't load bots/default_c.c",
    "lib 3 loaded cached skill 1.000000 from test_c.c",
    "lib 3 characteristic 40 is not initialized",
    "lib 3 counldn't load",
    "lib 1 couldn't load weights",
    "client 0 {\"kind\":\"failed\",\"stage\":\"item-weights\",\"errorCode\":9}",
    "print \"\\rarea 3, cluster 3       \"",
    "print \"\\r^1Solid!                              \"",
    "print \"\\remtpy area\"",
    "print \"\\r^1SOLID area\"",
    "print \"^1RED\\n\"",
    "print \"Sarge                  : accompanying anarki\\n\"",
    "print \"^4BLUE\\n\"",
};

/* ---- transcript ---- */
static char **lines;
static size_t line_count, line_capacity;
/* TS joins the transcript with "\n", so an embedded newline starts a new line. */
static void emit_line(const char *text) {
  for (;;) {
    const char *end = strchr(text, '\n');
    size_t length = end == NULL ? strlen(text) : (size_t)(end - text);
    if (line_count == line_capacity) {
      line_capacity = line_capacity ? line_capacity * 2 : 1024;
      lines = realloc(lines, line_capacity * sizeof *lines);
    }
    lines[line_count++] = strndup(text, length);
    if (end == NULL)
      return;
    text = end + 1;
  }
}
static void L(const char *format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  emit_line(buffer);
}
/* TS N(): integers plain (no -0), otherwise toFixed(6). Ring of buffers. */
static const char *N(double v) {
  static char ring[64][48];
  static int at;
  char *out = ring[at++ % 64];
  if (v == 0)
    strcpy(out, "0");
  else if (isfinite(v) && v == trunc(v))
    snprintf(out, 48, "%.0f", v);
  else
    snprintf(out, 48, "%.6f", v);
  return out;
}
static const char *B(bool v) { return v ? "1" : "0"; }
static char *json_quote(const char *text, char *out) {
  char *w = out;
  *w++ = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (*p == '"' || *p == '\\') {
      *w++ = '\\';
      *w++ = (char)*p;
    } else if (*p == '\n') {
      *w++ = '\\';
      *w++ = 'n';
    } else if (*p == '\r') {
      *w++ = '\\';
      *w++ = 'r';
    } else if (*p == '\t') {
      *w++ = '\\';
      *w++ = 't';
    } else if (*p < 0x20)
      w += sprintf(w, "\\u%04x", *p);
    else
      *w++ = (char)*p;
  }
  *w++ = '"';
  *w = '\0';
  return out;
}
static void trim_copy(const char *text, char *out) {
  while (*text == ' ' || *text == '\n' || *text == '\t' || *text == '\r')
    text++;
  strcpy(out, text);
  size_t n = strlen(out);
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n' || out[n - 1] == '\t' || out[n - 1] == '\r'))
    out[--n] = '\0';
}

/* ---- fixture ---- */
typedef struct fixture {
  qa_q3_base_product product;
  qa_identity_owner identity;
  qa_cvar_registry *cvars;
  char strings[1100][1024];
  char userinfo[64][1100];
  qa_q3_base_player_state players[8];
  qa_q3_config_string_store store;
  qa_q3_game_random random;
  qa_q3_game_memory memory;
  qa_bot_arsenal_interface_bots_a knowledge;
  qa_source_bot_game_bots_a game;
  qa_bot_navigation_bots_a navigation;
  qa_game_ai_host_bots_a host;
  qa_source_bot_library_options_bots_a library_options;
  qa_bot_library_bots_a library;
  qa_game_ai_context_bots_a context;
  qa_bot_state_bots_a *state;
  int32_t lib_seed;
} fixture;
static fixture *current;
#define SENTINEL(ctx) CHECK((ctx) == (void *)current)

static const char *files[][2] = {
    {"syn.c", ""},
    {"rnd.c", ""},
    {"rchat.c", ""},
    {"match.c", NULL},
    {"test_t.c", NULL},
    {"test_c.c", "skill 1 { 0 \"Sarge\" 1 \"male\" 21 \"test_t.c\" 22 \"test\" 24 0.5 25 0.25 27 1.0 28 "
                 "1.0 29 1.0 31 1.0 33 1.0 34 1.0 4 0.5 5 600 }"},
};
static const char *match_file =
    "16 { \"me\" = (100,0); }\n"
    "2 { 0, \" help \", 4 = (3,0); }\n"
    "2 { 0, \" dismiss\" = (19,0); }\n"
    "2 { 0, \" which team\" = (29,0); }\n"
    "2 { 0, \" join \", 4 = (13,0); }\n"
    "2 { 0, \" leave team\" = (14,0); }\n"
    "2 { 0, \" i lead\" = (8,128); }\n"
    "2 { 0, \" stop leading\" = (9,128); }\n"
    "2 { 0, \" kill \", 4 = (25,0); }\n"
    "2 { 0, \" feet \", 5 = (17,8); }\n"
    "2 { 0, \" space \", 5 = (17,0); }\n"
    "2 { 0, \" defender\" = (30,1); }\n"
    "2 { 0, \" who leads\" = (10,0); }\n"
    "2 { 0, \" doing\" = (12,0); }\n"
    "2 { 0, \" checkpoint \", 6, \" at \", 5 = (21,0); }\n"
    "2 { 0, \" get flag\" = (7,0); }\n"
    "2 { 0, \" formation\" = (15,0); }\n"
    "2 { 0, \" weird\" = (99,0); }\n"
    "2 { 0, \" my command\" = (28,0); }\n"
    "256 { 0, \" got the \", 1, \" flag\" = (300,4096); }\n"
    "256 { 0, \" captured\" = (300,8192); }";
static const struct {
  const char *name;
  int count;
} chats[] = {{"whois", 1}, {"whereareyou", 1}, {"whereis", 1}, {"dismissed", 0}, {"keepinmind", 1},
             {"joinedteam", 1}, {"leftteam", 1}, {"inteam", 1}, {"noteam", 0},
             {"checkpoint_invalid", 0}, {"checkpoint_confirm", 2}, {"helping", 1},
             {"accompanying", 1}, {"roaming", 0}, {"capturingflag", 0}, {"camping", 0},
             {"killing", 1}, {"whoisteamleader", 0}, {"iamteamleader", 0}, {"cmd_getflag", 1},
             {"cmd_defendbase", 1}, {"cmd_accompany", 2}, {"cmd_accompanyme", 1},
             {"cmd_returnflag", 1}, {"cmd_attackenemybase", 1}, {"cmd_harvest", 1},
             {"death_insult", 2}, {"death_praise", 2}, {"death_suicide", 1}, {"death_rail", 2},
             {"death_teammate", 1}, {"kill_insult", 1}, {"kill_praise", 1}, {"kill_rail", 1},
             {"random_misc", 6}, {"random_insult", 6}, {"game_enter", 5}, {"game_exit", 5},
             {"level_start", 1}, {"enemy_suicide", 1}, {"hit_talking", 2}, {"hit_nokill", 2},
             {"location", 1}, {"teamlocation", 2}};
static char chat_file[16384];
static void build_chat_file(void) {
  char *w = chat_file;
  w += sprintf(w, "chat \"test\" {");
  for (size_t i = 0; i < sizeof chats / sizeof chats[0]; i++) {
    w += sprintf(w, " type \"%s\" { \"%s\"", chats[i].name, chats[i].name);
    for (int v = 0; v < chats[i].count; v++)
      w += sprintf(w, ", \" %d=\", %d", v, v);
    w += sprintf(w, "; }");
  }
  sprintf(w, " }");
  files[3][1] = match_file;
  files[4][1] = chat_file;
}

static const uint8_t *read_file(void *ctx, const char *path, size_t *out_len) {
  SENTINEL(ctx);
  const char *name = path;
  if (strncasecmp(name, "botfiles/", 9) == 0)
    name += 9;
  else if (strncasecmp(name, "./botfiles/", 11) == 0)
    name += 11;
  else
    return NULL;
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++)
    if (strcasecmp(files[i][0], name) == 0) {
      *out_len = strlen(files[i][1]);
      return (const uint8_t *)files[i][1];
    }
  return NULL;
}
static int64_t lib_next_int(void *ctx) {
  SENTINEL(ctx);
  current->lib_seed = (int32_t)(((uint32_t)current->lib_seed * 1103515245u + 12345u) & 0x7fffffffu);
  return current->lib_seed;
}
static double lib_milliseconds(void *ctx) {
  SENTINEL(ctx);
  return 0;
}
static void lib_print(void *ctx, int severity, const char *text) {
  SENTINEL(ctx);
  char trimmed[4096];
  trim_copy(text, trimmed);
  L("lib %d %s", severity, trimmed);
}
static void lib_command(void *ctx, int64_t client, const char *command) {
  SENTINEL(ctx);
  L("cmd %lld %s", (long long)client, command);
}
static const char *cs_get(void *ctx, double index) {
  SENTINEL(ctx);
  return current->strings[(int)index];
}
static void cs_set(void *ctx, double index, const char *value) {
  SENTINEL(ctx);
  snprintf(current->strings[(int)index], 1024, "%s", value);
  L("cs %d %s", (int)index, value);
}
static void engine_print(void *ctx, const char *text) {
  SENTINEL(ctx);
  char quoted[8192];
  L("print %s", json_quote(text, quoted));
}
static const char *get_userinfo(void *ctx, int64_t client) {
  SENTINEL(ctx);
  return current->userinfo[client];
}
static void set_userinfo(void *ctx, int64_t client, const char *value) {
  SENTINEL(ctx);
  snprintf(current->userinfo[client], 1100, "%s", value);
  L("userinfo %lld %s", (long long)client, value);
}
static void insert_console(void *ctx, const char *text) {
  SENTINEL(ctx);
  L("insert %s", text);
}
static int64_t game_type(void *ctx) {
  SENTINEL(ctx);
  return current->context.deathmatch.gametype;
}
static int64_t game_max_clients(void *ctx) {
  SENTINEL(ctx);
  return 8;
}
static int64_t game_entity_count(void *ctx) {
  SENTINEL(ctx);
  return 1024;
}
static qa_q3_base_server_trace_result game_trace(void *ctx, qa_q3_base_server_trace_query query) {
  SENTINEL(ctx);
  qa_q3_base_server_trace_result result;
  memset(&result, 0, sizeof result);
  result.fraction = 1;
  result.entity_num = query.start.z > 50 ? 5 : 1022;
  return result;
}
static int64_t game_point_contents(void *ctx, qa_vec3 point, int64_t pass) {
  SENTINEL(ctx);
  (void)point;
  (void)pass;
  return 0;
}
static double clock_zero(void *ctx) {
  SENTINEL(ctx);
  return 0;
}
static qa_bot_observed_entity_bots_a game_entity(void *ctx, int64_t n) {
  SENTINEL(ctx);
  qa_bot_observed_entity_bots_a e;
  memset(&e, 0, sizeof e);
  qa_q3_entity_state_init(&e.state);
  e.present = n < 8;
  e.linked = true;
  e.bot = n != 3;
  if (n >= 0 && n < 8) {
    e.has_player = true;
    e.player_state = &current->players[n];
    e.player_connected = true;
    e.player_team = 1;
    e.player_name = "";
    e.last_hurt_client = 2;
    e.last_hurt_mod = 10;
  }
  return e;
}
static void userinfo_changed(void *ctx, int64_t client) {
  SENTINEL(ctx);
  L("changed %lld", (long long)client);
}
static double knowledge_aggression(void *ctx, struct qa_bot_state_bots_a *state) {
  SENTINEL(ctx);
  (void)state;
  return 60;
}
static qa_bot_weapon_tactics_bots_a knowledge_tactics(void *ctx, int64_t weapon) {
  SENTINEL(ctx);
  (void)weapon;
  qa_bot_weapon_tactics_bots_a t;
  memset(&t, 0, sizeof t);
  return t;
}
static bool nav_ready(void *ctx) {
  SENTINEL(ctx);
  return true;
}
static void nav_with_client(void *ctx, int64_t client, void (*think)(void *), void *think_ctx) {
  SENTINEL(ctx);
  (void)client;
  think(think_ctx);
}
static int64_t nav_point_area(void *ctx, qa_vec3 origin) {
  SENTINEL(ctx);
  return origin.z >= 0 ? (int64_t)floor(origin.z / 10) + 1 : 0;
}
static qa_bot_area_crossing *nav_trace_areas(void *ctx, qa_vec3 start, qa_vec3 end, int64_t maximum,
                                             size_t *out_count) {
  SENTINEL(ctx);
  (void)start;
  (void)end;
  (void)maximum;
  *out_count = 0;
  return NULL;
}
static double nav_travel(void *ctx, qa_area_travel_time_query q) {
  SENTINEL(ctx);
  return (double)(q.area * 10 + q.goal_area);
}
static qa_bounds nav_presence(void *ctx, int64_t presence) {
  SENTINEL(ctx);
  (void)presence;
  return (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
}
static qa_bot_navigation_area nav_area(void *ctx, int64_t number) {
  SENTINEL(ctx);
  (void)number;
  return (qa_bot_navigation_area){0, 0, 0, 3, 0};
}
static const char *host_console(void *ctx, int64_t client) {
  SENTINEL(ctx);
  (void)client;
  return NULL;
}
static double memory_debug(void *ctx) {
  SENTINEL(ctx);
  return 0;
}
static void memory_print(void *ctx, const char *text) {
  SENTINEL(ctx);
  (void)text;
}

static const char *const names[8] = {"n\\Sarge\\t\\1", "n\\Mr [TAG]Anarki\\t\\1", "n\\Klesk\\t\\2",
                                     "n\\Human^1Bob\\t\\1", "n\\Doom\\t\\2", "", "", ""};

static fixture *env(qa_q3_base_product product, int64_t game_type_value) {
  fixture *f = calloc(1, sizeof *f);
  current = f;
  f->product = product;
  f->lib_seed = 12345;
  CHECK(qa_identity_owner_create(&f->identity, "oracle"));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = f->identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  f->cvars = qa_cvar_registry_create(&options);
  qa_cvar_snapshot snapshot;
  const char *registered[][2] = {{"sv_maxclients", "8"}, {"bot_fastchat", "0"}, {"bot_nochat", "0"}};
  for (size_t i = 0; i < 3; i++) {
    CHECK(qa_cvar_registry_register(f->cvars, registered[i][0], registered[i][1], 0, &snapshot));
    qa_cvar_read_free(&snapshot);
  }
  CHECK(qa_cvar_registry_register(f->cvars, "mapname", "q3dm17", 4, &snapshot));
  qa_cvar_read_free(&snapshot);
  for (int i = 0; i < 8; i++)
    snprintf(f->strings[544 + i], 1024, "%s", names[i]);
  static const double scores[8] = {5, 9, 1, 3, 7, 0, 0, 0};
  static const double heights[8] = {0, 30, 10, 50, 0, 20, 40, 0};
  for (int i = 0; i < 8; i++) {
    CHECK(qa_q3_base_player_state_init(&f->players[i], product, NULL));
    qa_q3_base_player_state_slots_set(&f->players[i].persistant, 0, scores[i]);
    qa_q3_base_player_state_record_set_origin(&f->players[i], (qa_vec3){0, 0, heights[i]});
  }
  f->store = (qa_q3_config_string_store){f, cs_get, cs_set};
  qa_game_random_init(&f->random, 7);
  qa_q3_game_memory_init(&f->memory, f, memory_debug, memory_print);
  f->knowledge.ctx = f;
  f->knowledge.aggression = knowledge_aggression;
  f->knowledge.tactics = knowledge_tactics;
  qa_source_bot_game_bots_a *g = &f->game;
  g->ctx = f;
  g->options.product = product;
  g->options.cvars = f->cvars;
  g->options.configstrings = &f->store;
  g->options.engine.ctx = f;
  g->options.engine.print = engine_print;
  g->options.engine.get_userinfo = get_userinfo;
  g->options.engine.set_userinfo = set_userinfo;
  g->options.engine.insert_console_command = insert_console;
  g->game_type = game_type;
  g->max_clients = game_max_clients;
  g->entity_count = game_entity_count;
  g->trace = game_trace;
  g->point_contents = game_point_contents;
  g->random = qa_source_bot_game_random_bots_a(&f->random);
  g->clock_time = clock_zero;
  g->clock_start_time = clock_zero;
  g->clock_intermission_time = clock_zero;
  g->memory = &f->memory;
  g->knowledge = &f->knowledge;
  g->entity = game_entity;
  g->client_userinfo_changed = userinfo_changed;
  f->navigation.ctx = f;
  f->navigation.ready = nav_ready;
  f->navigation.with_client = nav_with_client;
  f->navigation.point_area = nav_point_area;
  f->navigation.trace_areas = nav_trace_areas;
  f->navigation.area_travel_time_to_goal = nav_travel;
  f->navigation.presence_bounds = nav_presence;
  f->navigation.get_area = nav_area;
  f->host.ctx = f;
  f->host.navigation = &f->navigation;
  f->host.get_console_message = host_console;
  f->host.insert_console_command = insert_console;
  f->library_options.ctx = f;
  f->library_options.read_file = read_file;
  f->library_options.random = (qa_bot_random){lib_next_int, f};
  f->library_options.milliseconds = lib_milliseconds;
  f->library_options.print = lib_print;
  f->library_options.client_command = lib_command;
  qa_bot_library_bots_a_init(&f->library, &f->library_options);
  qa_bot_action_buffer_setup(&f->library.actions, 64);
  qa_bot_chat_library_setup(&f->library.chat);
  CHECK(qa_game_ai_context_bots_a_init(&f->context, &f->game, &f->library, &f->host));
  f->context.deathmatch.gametype = game_type_value;
  f->context.deathmatch.maxclients = 8;
  f->context.time = 100;
  qa_bot_state_bots_a *s = qa_bot_state_store_acquire(&f->context.states, 0, &f->memory);
  f->state = s;
  s->src->inuse = 1;
  s->src->client = 0;
  s->src->entity_num = 0;
  s->src->cs = (int32_t)qa_bot_chat_library_allocate(&f->library.chat);
  L("load %s", B(qa_bot_chat_library_load_chat_file(&f->library.chat, s->src->cs,
                                                    qa_chat_text("test_t.c"), qa_chat_text("test"))));
  s->src->character = (int32_t)qa_bot_character_load(&f->library.characters, "test_c.c", 1);
  L("character %d", s->src->character);
  return f;
}

static void env_destroy(fixture *f) {
  qa_game_ai_context_bots_a_destroy(&f->context);
  qa_bot_library_shutdown_bots_a(&f->library);
  qa_bot_library_bots_a_destroy(&f->library);
  for (int i = 0; i < 8; i++)
    qa_q3_base_player_state_record_destroy(&f->players[i]);
  qa_cvar_registry_destroy(f->cvars);
  qa_identity_owner_destroy(&f->identity);
  free(f);
  current = NULL;
}

static void S(const char *tag, qa_bot_state_bots_a *state) {
  const qa_bot_state_source *s = state->src;
  L("%s %s %s %s %s %s %s %s [%s] [%s] %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
    tag, N(s->ltg_type), N(s->team_goal_time), N(s->decisionmaker), B(s->ordered), N(s->order_time),
    N(s->team_message_time), N(s->teammate), s->team_leader, s->subteam, N(s->formation_dist),
    N(s->red_flag_status), N(s->blue_flag_status), N(s->flag_carrier), N(s->last_flag_capture_time),
    N(s->own_decision_time), N(s->team_goal.entity), N(s->team_goal.area), N(s->last_chat_time),
    N(s->chat_to), N(s->ask_team_leader_time), N(s->become_team_leader_time),
    N(s->team_give_orders_time), N(s->num_teammates), N(s->ctf_strategy), B(s->force_orders),
    N(s->lead_time), N(s->last_goal_ltg_type), N(s->ctf_roam_time), N(s->rush_base_away_time),
    N(s->alt_route_goal.area));
}

static const char *stristr_text(const char *text, const char *part) {
  const char *found = qa_stristr_bots_a(text, part);
  return found == NULL ? "null" : found;
}

static void scenario_orders(void) {
  fixture *e = env(QA_Q3_BASE_PRODUCT_MISSIONPACK, 4);
  qa_game_ai_context_bots_a *c = &e->context;
  qa_bot_state_bots_a *s = e->state;
  static const int64_t clients[] = {0, 1, 3, 7, 64, -1};
  for (size_t i = 0; i < 6; i++) {
    char a[1024], b[1024], d[1024], k[1024];
    qa_client_name_bots_a(c, clients[i], 36, a);
    qa_client_name_bots_a(c, clients[i], 4, b);
    qa_easy_client_name_bots_a(c, clients[i], 36, d);
    qa_client_skin_bots_a(c, clients[i], 128, k);
    L("name %lld [%s] [%s] [%s] [%s]", (long long)clients[i], a, b, d, k);
  }
  static const char *const wanted[] = {"sarge", "MR [TAG]ANARKI", "humanbob", "nobody"};
  for (size_t i = 0; i < 4; i++) {
    int64_t from = qa_client_from_name_bots_a(c, wanted[i]);
    int64_t same = qa_client_on_same_team_from_name_bots_a(c, s, wanted[i]);
    L("from %s %lld %lld", wanted[i], (long long)from, (long long)same);
  }
  L("team %s %s %s %s %s %s", N(qa_bot_team_bots_a(c, s)), N(qa_bot_opposite_team_bots_a(c, s)),
    B(qa_bot_same_team_bots_a(c, s, 1)), B(qa_bot_same_team_bots_a(c, s, 2)),
    B(qa_bot_same_team_bots_a(c, s, 64)), N(qa_bot_synonym_context_bots_a(c, s)));
  L("stristr %s %s %s %s", stristr_text("Hello World", "WORLD"), stristr_text("", ""),
    stristr_text("abc", ""), stristr_text("abc", "x"));
  char clean[64], removed[64];
  qa_bot_clean_string_bots_a("^1Red^^7 o\x7fk^", clean, sizeof clean);
  qa_remove_color_escape_sequences_bots_a("^1Red^^7 o\x7fk^", removed, sizeof removed);
  L("clean [%s] [%s]", clean, removed);
  qa_bot_set_user_info_bots_a(c, s, "teamtask", "4");
  s->src->ltg_type = 2;
  s->src->teammate = 1;
  qa_bot_set_team_status_bots_a(c, s);
  env_destroy(e);
}

static void scenario_seek(qa_q3_base_product product) {
  fixture *e = env(product, 4);
  qa_game_ai_context_bots_a *c = &e->context;
  qa_bot_state_bots_a *s = e->state;
  c->deathmatch.ctf_red_flag.area = 7;
  c->deathmatch.ctf_blue_flag.area = 9;
  c->deathmatch.ctf_blue_flag.number = 3;
  static const int rows[][3] = {{0, 0, 100}, {0, 1, 110}, {1, 0, 120}, {1, 1, 130}, {0, 0, 140}, {0, 0, 200}};
  for (size_t i = 0; i < 6; i++) {
    s->src->red_flag_status = rows[i][0];
    s->src->blue_flag_status = rows[i][1];
    c->time = rows[i][2];
    qa_bot_team_goals_bots_a(c, s, false);
    S("seek", s);
  }
  s->src->inventory[45] = 1;
  qa_bot_team_goals_bots_a(c, s, true);
  S("retreat", s);
  qa_bot_team_goals_bots_a(c, s, false);
  S("carry", s);
  s->src->inventory[45] = 0;
  s->src->last_goal_ltg_type = 6;
  s->src->ordered = 1;
  s->src->order_time = 199;
  c->time = 205;
  qa_bot_refuse_order_bots_a(c, s);
  S("refuse", s);
  L("set %s", B(qa_bot_set_last_ordered_task_bots_a(c, s)));
  S("set", s);
  env_destroy(e);
}

static void scenario_team(qa_q3_base_product product, int64_t gt) {
  fixture *e = env(product, gt);
  qa_game_ai_context_bots_a *c = &e->context;
  qa_bot_state_bots_a *s = e->state;
  c->deathmatch.ctf_red_flag.area = 7;
  c->deathmatch.ctf_blue_flag.area = 9;
  c->deathmatch.red_obelisk.area = 11;
  c->deathmatch.blue_obelisk.area = 13;
  snprintf(e->strings[544 + 5], 1024, "n\\Xaero\\t\\1");
  snprintf(e->strings[544 + 6], 1024, "n\\Hunter\\t\\1");
  c->command.not_leader[3] = true;
  static const double times[] = {100, 120, 140};
  for (size_t i = 0; i < 3; i++) {
    c->time = times[i];
    qa_bot_team_ai_bots_a(c, s);
    S("teamai", s);
  }
  qa_bot_set_team_mate_task_preference_bots_a(c, s, 5, 1);
  int64_t p5 = qa_bot_get_team_mate_task_preference_bots_a(c, s, 5);
  int64_t p6 = qa_bot_get_team_mate_task_preference_bots_a(c, s, 6);
  L("pref %s %s %s", N((double)p5), N((double)p6), N((double)qa_bot_num_team_mates_bots_a(c, s)));
  static const int rows[][4] = {{0, 0, 0, 200}, {1, 0, 1, 260}, {0, 1, 2, 320}, {1, 1, 3, 600}};
  for (size_t i = 0; i < 4; i++) {
    s->src->red_flag_status = rows[i][0];
    s->src->blue_flag_status = rows[i][1];
    s->src->neutral_flag_status = rows[i][2];
    s->src->flag_status_changed = 1;
    s->src->flag_carrier = 1;
    c->time = rows[i][3];
    qa_bot_team_ai_bots_a(c, s);
    c->time = rows[i][3] + 10;
    qa_bot_team_ai_bots_a(c, s);
    S("orders", s);
  }
  env_destroy(e);
}

static void message_at(fixture *e, const char *message) {
  e->context.time += 1;
  bool matched = qa_bot_match_message_bots_a(&e->context, e->state, message);
  L("match %s %s", message, B(matched));
  S("m", e->state);
}

static void scenario_commands(qa_q3_base_product product) {
  fixture *e = env(product, 4);
  qa_game_ai_context_bots_a *c = &e->context;
  qa_bot_state_bots_a *s = e->state;
  c->deathmatch.ctf_red_flag.area = 7;
  c->deathmatch.ctf_blue_flag.area = 9;
  c->deathmatch.ctf_neutral_flag.area = 4;
  qa_bot_entity_update update;
  memset(&update, 0, sizeof update);
  update.has_generation = true;
  update.generation = 1;
  update.type = 1;
  update.origin = (qa_vec3){10, 20, 30};
  update.solid = 2;
  qa_bot_entity_observations_update(&c->observations, 1, &update, 1);
  static const char *const voices[] = {
      "0 1 0 getflag",     "0 1 0 offense",     "0 1 0 DEFEND",      "0 1 0 patrol",
      "0 1 0 camp",        "0 1 0 followme",    "0 1 0 returnflag",  "0 3 0 startleader",
      "0 3 0 whoisleader", "0 3 0 stopleader",  "0 1 0 wantondefense", "0 1 0 wantonoffense",
      "0 2 0 getflag",     "0 1 0 bogus"};
  for (size_t i = 0; i < sizeof voices / sizeof voices[0]; i++) {
    bool handled = qa_bot_voice_chat_command_bots_a(c, s, 1, voices[i]);
    L("voice %s %s", voices[i], B(handled));
    S("v", s);
  }
  L("voice0 %s", B(qa_bot_voice_chat_command_bots_a(c, s, 0, "0 1 0 getflag")));
  static const char *const messages[] = {
      "Mr [TAG]Anarki help me", "Mr [TAG]Anarki help Klesk", "Human^1Bob help Mr [TAG]Anarki",
      "Mr [TAG]Anarki dismiss", "Mr [TAG]Anarki which team", "Mr [TAG]Anarki join alpha",
      "Mr [TAG]Anarki which team", "Mr [TAG]Anarki leave team", "Sarge i lead",
      "Mr [TAG]Anarki who leads", "Mr [TAG]Anarki defender", "Mr [TAG]Anarki stop leading",
      "Sarge stop leading", "Mr [TAG]Anarki kill Doom", "Mr [TAG]Anarki kill Nobody",
      "Mr [TAG]Anarki feet 10", "Mr [TAG]Anarki space 2", "Mr [TAG]Anarki space 99",
      "Mr [TAG]Anarki doing", "Mr [TAG]Anarki checkpoint base at 1 2 3",
      "Mr [TAG]Anarki checkpoint base at 4 5 -9", "Mr [TAG]Anarki checkpoint base at 7 8 9",
      "Mr [TAG]Anarki get flag", "Klesk got the red flag", "Mr [TAG]Anarki got the blue flag",
      "Klesk captured", "Mr [TAG]Anarki formation", "Mr [TAG]Anarki weird", "Sarge my command",
      "nothing matches"};
  for (size_t i = 0; i < sizeof messages / sizeof messages[0]; i++)
    message_at(e, messages[i]);
  qa_vec3 gps = qa_bot_gps_to_position_bots_a(c, " -12 34x56");
  L("gps {\"x\":%s,\"y\":%s,\"z\":%s}", N(gps.x), N(gps.y), N(gps.z));
  int64_t f1 = qa_find_client_by_name_bots_a(c, "klesk");
  int64_t f2 = qa_find_client_by_name_bots_a(c, "ANAR");
  int64_t f3 = qa_find_enemy_by_name_bots_a(c, s, "o");
  int64_t f4 = qa_num_players_on_same_team_bots_a(c, s);
  L("find %s %s %s %s", N((double)f1), N((double)f2), N((double)f3), N((double)f4));
  s->src->ltg_type = 3;
  qa_bot_print_team_goal_bots_a(c, s);
  s->src->ltg_type = 0;
  s->src->ctf_roam_time = 300;
  qa_bot_print_team_goal_bots_a(c, s);
  s->src->ctf_roam_time = 0;
  qa_bot_print_team_goal_bots_a(c, s);
  L("leader %s", B(qa_bot_team_leader_bots_a(c, s)));
  qa_bot_report_status_bots_a(c, s);
  s->src->ltg_type = 4;
  s->src->inventory[45] = 1;
  qa_bot_report_status_bots_a(c, s);
  qa_bot_update_info_config_strings_bots_a(c);
  env_destroy(e);
}

static const char *library_message(fixture *e, char *out) {
  char *text = qa_bot_chat_library_get_chat_message(&e->library.chat, e->state->src->cs, 256);
  sprintf(out, "[%s]", text);
  free(text);
  return out;
}

static void scenario_chat(qa_q3_base_product product) {
  fixture *e = env(product, 0);
  qa_game_ai_context_bots_a *c = &e->context;
  qa_bot_state_bots_a *s = e->state;
  char m[512], first[32], last[32], title[128];
  int64_t active = qa_bot_num_active_players_bots_a(c);
  bool is_first = qa_bot_is_first_in_rankings_bots_a(c, s);
  bool is_last = qa_bot_is_last_in_rankings_bots_a(c, s);
  qa_bot_first_client_in_rankings_bots_a(c, first);
  qa_bot_last_client_in_rankings_bots_a(c, last);
  L("active %s %s %s %s %s", N((double)active), B(is_first), B(is_last), first, last);
  bool valid = qa_bot_valid_chat_position_bots_a(c, s);
  bool visible = qa_bot_visible_enemies_bots_a(c, s);
  L("valid %s %s [%s]", B(valid), B(visible), qa_bot_map_title_bots_a(c, title));
  for (int method = 0; method < 30; method++)
    L("weapon %d %s", method, qa_bot_weapon_name_for_means_of_death_bots_a(c, method));
  for (int i = 0; i < 6; i++) {
    const char *weapon = qa_bot_random_weapon_name_bots_a(c);
    char opponent[32];
    qa_bot_random_opponent_name_bots_a(c, s, opponent);
    L("rw %s %s", weapon, opponent);
  }
  static const int deaths[][3] = {{10, 2, 100}, {10, 2, 130}, {2, 2, 160}, {14, 2, 190},
                                  {18, 2, 220}, {26, 0, 250}, {7, 1, 280}};
  for (size_t i = 0; i < 7; i++) {
    s->src->bot_death_type = deaths[i][0];
    s->src->last_killed_by = deaths[i][1];
    c->time = deaths[i][2];
    bool result = qa_bot_chat_death_bots_a(c, s);
    L("death %s %s", B(result), library_message(e, m));
    S("d", s);
  }
  static const int kills[][3] = {{10, 2, 400}, {2, 4, 430}, {7, 0, 460}, {5, 2, 490}};
  for (size_t i = 0; i < 4; i++) {
    s->src->enemy_death_type = kills[i][0];
    s->src->last_killed_player = kills[i][1];
    c->time = kills[i][2];
    bool result = qa_bot_chat_kill_bots_a(c, s);
    L("kill %s %s", B(result), library_message(e, m));
  }
  static const double times[] = {600, 700, 800, 900};
  for (size_t i = 0; i < 4; i++) {
    c->time = times[i];
    s->src->think_time = 9;
    s->src->enemy = 2;
    bool a = qa_bot_chat_enter_game_bots_a(c, s);
    bool b = qa_bot_chat_exit_game_bots_a(c, s);
    bool d = qa_bot_chat_start_level_bots_a(c, s);
    bool f = qa_bot_chat_enemy_suicide_bots_a(c, s);
    bool g = qa_bot_chat_hit_talking_bots_a(c, s);
    bool h = qa_bot_chat_hit_no_kill_bots_a(c, s);
    bool r = qa_bot_chat_random_bots_a(c, s);
    L("misc %s %s %s %s %s %s %s %s", B(a), B(b), B(d), B(f), B(g), B(h), B(r), library_message(e, m));
  }
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_set(e->cvars, "bot_fastchat", "1", true, &snapshot));
  qa_cvar_read_free(&snapshot);
  qa_bot_cvar_register_bots_a(qa_game_ai_context_cvar(c, "bot_fastchat"), "bot_fastchat", "1", 0);
  c->time = 1000;
  bool fast = qa_bot_chat_random_bots_a(c, s);
  L("fast %s %s", B(fast), library_message(e, m));
  L("chattime %s", N(qa_bot_chat_time_bots_a(c, s)));
  env_destroy(e);
}

static void scenario_input(void) {
  fixture *e = env(QA_Q3_BASE_PRODUCT_BASEQ3, 0);
  qa_game_ai_context_bots_a *c = &e->context;
  qa_bot_state_bots_a *s = e->state;
  qa_bot_input inputs[3];
  memset(inputs, 0, sizeof inputs);
  inputs[0] = (qa_bot_input){(float)0.05, {1, 0, 0}, 400, {10, 90, 0}, 0x8000 | 0x1, 5};
  inputs[1] = (qa_bot_input){(float)0.05, {0.6, 0.8, 0.25}, 200, {-30, 200.5, 3},
                             0x100000 | 0x2000 | 0x80 | 0x8 | 0x8000000, 269};
  inputs[2] = (qa_bot_input){(float)0.05, {0, 0, 0}, 0, {359.9, -0.1, 0},
                             0x200 | 0x800 | 0x1000 | 0x10 | 0x10000 | 0x20000 | 0x2 | 0x80000 |
                                 0x200000 | 0x800000 | 0x1000000 | 0x2000000,
                             0};
  for (int i = 0; i < 3; i++) {
    qa_bot_input_to_user_command(inputs[i], &s->src->last_ucmd, (qa_bot_ivec3){1000, -2000, 70000},
                                 12345);
    const qa_bot_user_command_source *u = &s->src->last_ucmd;
    L("ucmd %s %s %s %s %s %s %s %s %s", N(u->server_time), N(u->angles.x), N(u->angles.y),
      N(u->angles.z), N(u->buttons), N(u->weapon), N(u->forwardmove), N(u->rightmove), N(u->upmove));
  }
  s->src->cur_ps.delta_angles = (qa_bot_ivec3){1000, -2000, 70000};
  s->src->viewangles = (qa_bot_fvec3){10, 350, (float)0.5};
  qa_bot_add_delta_angles(s);
  L("add %s %s %s", N(s->src->viewangles.x), N(s->src->viewangles.y), N(s->src->viewangles.z));
  qa_bot_subtract_delta_angles(s);
  L("sub %s %s %s", N(s->src->viewangles.x), N(s->src->viewangles.y), N(s->src->viewangles.z));
  L("diff %s %s %s %s", N(qa_bot_angle_difference(10, 350)), N(qa_bot_angle_difference(350, 10)),
    N(qa_bot_change_view_angle(350, 10, 5)), N(qa_bot_change_view_angle(10, 350, 30)));
  static const int enemies[] = {-1, 2};
  for (int i = 0; i < 2; i++) {
    s->src->enemy = enemies[i];
    s->src->ideal_viewangles = (qa_bot_fvec3){200, 45, 0};
    s->src->viewangles = (qa_bot_fvec3){10, 300, 0};
    s->src->viewangle_speed = (qa_bot_fvec3){3, -4, 0};
    for (int step = 0; step < 3; step++) {
      qa_bot_change_view_angles(c, s, 0.1);
      L("view %s %s %s %s %s", N(s->src->viewangles.x), N(s->src->viewangles.y),
        N(s->src->ideal_viewangles.x), N(s->src->viewangle_speed.x), N(s->src->viewangle_speed.y));
    }
  }
  qa_bot_action_action(&e->library.actions, 0, 0x8 | 0x1);
  s->src->last_ucmd.buttons = 1;
  qa_bot_update_input_bots_a(c, s, 20000, 50);
  const qa_bot_user_command_source *u = &s->src->last_ucmd;
  L("update %s %s %s %s %s %s %s %s", N(u->server_time), N(u->angles.x), N(u->angles.y),
    N(u->angles.z), N(u->buttons), N(u->forwardmove), N(s->src->viewangles.x),
    N(s->src->viewangles.y));
  env_destroy(e);
}

/* ---- arsenal knowledge ---- */
static qa_bot_weapon_knowledge_bots_a arsenal[5];
static qa_weapon_info_bots_a weapon_info(int number) {
  qa_weapon_info_bots_a info;
  memset(&info, 0, sizeof info);
  info.valid = true;
  info.number = number;
  info.projectile_count = 1;
  info.ammo_amount = 1;
  info.reload = 0.5f;
  info.projectile_info.damage = 10;
  return info;
}
static void build_arsenal(void) {
  memset(arsenal, 0, sizeof arsenal);
  arsenal[0].info = weapon_info(1);
  arsenal[0].info.weapon_inventory_index = 4;
  arsenal[0].info.ammo_amount = 0;
  arsenal[0].has_maximum_range = true;
  arsenal[0].maximum_range = 64;
  arsenal[0].melee = true;
  arsenal[1].info = weapon_info(2);
  arsenal[1].info.weapon_inventory_index = 6;
  arsenal[1].info.ammo_inventory_index = 19;
  arsenal[1].info.horizontal_spread = 1;
  arsenal[1].has_supply = true;
  arsenal[1].supply = (qa_bot_weapon_supply_bots_a){"q3:machinegun", true, true, "q3:bullets", 1};
  arsenal[2].info = weapon_info(5);
  arsenal[2].info.weapon_inventory_index = 8;
  arsenal[2].info.ammo_inventory_index = 23;
  arsenal[2].info.speed = 900;
  memset(&arsenal[2].info.projectile_info, 0, sizeof arsenal[2].info.projectile_info);
  arsenal[2].info.projectile_info.damage = 100;
  arsenal[2].info.projectile_info.damage_type = 2;
  arsenal[2].has_supply = true;
  arsenal[2].supply = (qa_bot_weapon_supply_bots_a){"q3:rocketlauncher", false, true, "q3:rockets", 1};
  arsenal[3].info = weapon_info(7);
  arsenal[3].info.weapon_inventory_index = 10;
  arsenal[3].info.ammo_inventory_index = 24;
  arsenal[3].info.reload = 1.5f;
  arsenal[3].has_personality_role = true;
  arsenal[3].personality_role = 7;
  arsenal[4].info = weapon_info(4);
  arsenal[4].info.weapon_inventory_index = 7;
  arsenal[4].info.ammo_inventory_index = 20;
  memset(&arsenal[4].info.projectile_info, 0, sizeof arsenal[4].info.projectile_info);
  arsenal[4].info.projectile_info.gravity = 800;
  arsenal[4].info.projectile_info.damage = 100;
  arsenal[4].info.projectile_info.damage_type = 2;
  arsenal[4].has_maximum_range = true;
  arsenal[4].maximum_range = 300;
}
static void arsenal_update(void *ctx, struct qa_bot_state_bots_a *state) {
  SENTINEL(ctx);
  (void)state;
  L("update-inventory");
}
static qa_bot_weapon_knowledge_bots_a *arsenal_candidates(void *ctx, struct qa_bot_library_bots_a *library,
                                                         int64_t handle, size_t *out_count) {
  SENTINEL(ctx);
  CHECK(library == &current->library);
  (void)handle;
  qa_bot_weapon_knowledge_bots_a *copy = malloc(sizeof arsenal);
  memcpy(copy, arsenal, sizeof arsenal);
  *out_count = 5;
  return copy;
}
static const char *optional(bool has, double value, char *out) {
  if (!has)
    return "null";
  strcpy(out, N(value));
  return out;
}

static void scenario_arsenal(void) {
  fixture *e = env(QA_Q3_BASE_PRODUCT_MISSIONPACK, 0);
  qa_bot_state_bots_a *s = e->state;
  build_arsenal();
  qa_bot_arsenal_data_bots_a data = {e, arsenal_update, arsenal_candidates};
  qa_bot_arsenal_knowledge_bots_a k;
  qa_create_bot_arsenal_knowledge_bots_a(&k, &data);
  qa_bot_arsenal_interface_bots_a *kn = &k.knowledge;
  qa_bot_library_bots_a *lib = &e->library;
  static const int slots[][2] = {{6, 1}, {19, 50}, {4, 1}, {10, 1}, {24, 3}, {29, 100}, {1, 50}};
  for (size_t i = 0; i < 7; i++)
    s->src->inventory[slots[i][0]] = slots[i][1];
  int64_t activation = kn->activation_weapon(kn->ctx, lib, s);
  int64_t chosen = kn->choose_weapon(kn->ctx, lib, s);
  double aggression = kn->aggression(kn->ctx, s);
  L("activation %s %s %s", N((double)activation), N((double)chosen), N(aggression));
  static const int weapons[] = {1, 2, 5, 7, 4, 9};
  for (size_t i = 0; i < 6; i++) {
    qa_bot_weapon_tactics_bots_a t = kn->tactics(kn->ctx, weapons[i]);
    char a[48], b[48], d[48];
    L("tactics %d %s %s %s %s %s %s", weapons[i], B(t.melee), optional(t.has_max_range, t.max_range, a),
      optional(t.has_aim_accuracy, t.aim_accuracy, b), optional(t.has_aim_skill, t.aim_skill, d),
      N(t.weakness), B(t.predict_occluded_splash));
  }
  qa_weapon_info_bots_a info;
  bool has5 = kn->weapon_info(kn->ctx, lib, 0, 5, &info);
  char n5[48];
  snprintf(n5, sizeof n5, "%s", has5 ? N(info.number) : "undefined");
  bool has3 = kn->weapon_info(kn->ctx, lib, 0, 3, &info);
  L("info %s %s", n5, has3 ? N(info.number) : "undefined");
  qa_pickup_ammo_receipt weapons_given[] = {{"q3:rocketlauncher", 0, 1}, {"q3:rocketlauncher", 0, 1}};
  qa_pickup_ammo_receipt ammo_given[] = {{"q3:rockets", 0, 10}, {"q3:bullets", 50, 40}, {"q3:cells", 0, 5},
                                         {"q3:rockets", 0, 2}};
  qa_pickup_supply_preview preview = {true, ammo_given, 4, weapons_given, 2};
  qa_pickup_supply_preview refused = {false, NULL, 0, NULL, 0};
  double u1 = kn->pickup_utility(kn->ctx, lib, s, &preview);
  double u2 = kn->pickup_utility(kn->ctx, lib, s, &refused);
  L("utility %s %s", N(u1), N(u2));
  s->src->inventory[35] = 1;
  L("quad %s", N(kn->aggression(kn->ctx, s)));
  s->src->inventory[35] = 0;
  s->src->inventory[24] = 0;
  s->src->inventory[19] = 0;
  L("aggr %s", N(kn->aggression(kn->ctx, s)));
  kn->update_inventory(kn->ctx, s);
  qa_bot_arsenal_knowledge_bots_a_destroy(&k);
  env_destroy(e);
}

static void host_spawn(void *ctx) {
  SENTINEL(ctx);
  L("spawn");
}
static void host_user_command(void *ctx, int64_t client, const qa_q3_base_user_command *u) {
  SENTINEL(ctx);
  L("usercmd %lld %s %s %s", (long long)client, N(u->server_time), N(u->buttons), N(u->forwardmove));
}
static int64_t host_load_map(void *ctx, const char *name) {
  SENTINEL(ctx);
  (void)name;
  return 0;
}
static void registry_set(fixture *e, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_set(e->cvars, name, value, true, &snapshot));
  qa_cvar_read_free(&snapshot);
}
static const char *const setup_kinds[] = {"empty", "setting-up", "failed", "complete"};
static const char *const setup_stages[] = {"allocated", "character", "settings", "goal-state",
                                           "item-weights", "weapon-state", "weapon-weights",
                                           "chat-state", "chat-file", "chat-gender", "published",
                                           "move-state", "walker", "counted", "scheduled",
                                           "interbred", "session", "aas"};

static void scenario_main(void) {
  fixture *e = env(QA_Q3_BASE_PRODUCT_MISSIONPACK, 4);
  e->host.check_bot_spawn = host_spawn;
  e->host.user_command = host_user_command;
  e->host.load_map = host_load_map;
  qa_game_ai_bots_a *ai = calloc(1, sizeof *ai);
  CHECK(qa_game_ai_bots_a_init(ai, &e->game, &e->library, &e->host));
  qa_game_ai_context_bots_a *c = &ai->context;
  L("setup %s", B(qa_game_ai_setup_bots_a(ai, true)));
  qa_bot_state_bots_a *s = qa_bot_state_store_acquire(&c->states, 0, &e->memory);
  s->src->inuse = 1;
  s->src->client = 0;
  s->src->entity_num = 0;
  s->src->ltg_type = 2;
  s->src->teammate = 1;
  s->src->last_ucmd.forwardmove = 50;
  s->src->last_ucmd.buttons = 3;
  c->deathmatch.maxclients = 8;
  registry_set(e, "bot_pause", "1");
  registry_set(e, "bot_report", "1");
  L("frame %s", B(qa_game_ai_start_frame_bots_a(ai, 1000)));
  qa_bot_settings_bots_a settings;
  memset(&settings, 0, sizeof settings);
  strcpy(settings.characterfile, "test_c.c");
  settings.skill = 1;
  strcpy(settings.team, "red");
  L("client %s", B(qa_game_ai_setup_client_bots_a(ai, 0, &settings, false)));
  strcpy(settings.characterfile, "missing_c.c");
  bool missing = qa_game_ai_setup_client_bots_a(ai, 1, &settings, false);
  qa_bot_state_bots_a *one = qa_bot_state_store_get(&c->states, 1);
  L("client %s %s", B(missing), one == NULL ? "none" : setup_kinds[one->setup.kind]);
  strcpy(settings.characterfile, "test_c.c");
  settings.skill = 2;
  strcpy(settings.team, "blue");
  bool second = qa_game_ai_setup_client_bots_a(ai, 2, &settings, false);
  qa_bot_state_bots_a *two = qa_bot_state_store_get(&c->states, 2);
  L("client %s {\"kind\":\"%s\",\"stage\":\"%s\",\"errorCode\":%lld}", B(second),
    setup_kinds[two->setup.kind], setup_stages[two->setup.stage], (long long)two->setup.error_code);
  qa_game_ai_interbreed_end_match_bots_a(ai);
  qa_game_ai_test_aas_bots_a(ai, (qa_vec3){0, 0, 25});
  registry_set(e, "bot_testclusters", "1");
  qa_game_ai_test_aas_bots_a(ai, (qa_vec3){0, 0, 25});
  qa_game_ai_test_aas_bots_a(ai, (qa_vec3){0, 0, -5});
  registry_set(e, "bot_testsolid", "1");
  qa_game_ai_test_aas_bots_a(ai, (qa_vec3){0, 0, 25});
  qa_game_ai_test_aas_bots_a(ai, (qa_vec3){0, 0, -5});
  qa_bot_teamplay_report_bots_a(c);
  /* TS startFrame: fractional milliseconds are a RangeError. */
  CHECK_ABORTS(qa_game_ai_start_frame_bots_a(ai, 1.5));
  qa_game_ai_bots_a_destroy(ai);
  free(ai);
  env_destroy(e);
}

static void test_aborts(void) {
  fixture *e = env(QA_Q3_BASE_PRODUCT_BASEQ3, 3);
  int64_t teammates[64];
  /* TS element(): a group from an empty teammate list reads index 0. */
  CHECK_ABORTS(qa_bot_create_group_bots_a(&e->context, e->state, teammates, 0, 2));
  /* TS botSortTeamMatesByBaseTravelTime throws in base-game GT_TEAM. */
  CHECK_ABORTS(qa_bot_sort_team_mates_by_base_travel_time_bots_a(&e->context, e->state, teammates, 4));
  /* TS botSetTeamMateTaskPreference: client outside the 64 preference cells. */
  CHECK_ABORTS(qa_bot_set_team_mate_task_preference_bots_a(&e->context, e->state, 64, 1));
  env_destroy(e);
  for (size_t i = 0; i < line_count; i++)
    free(lines[i]);
  line_count = 0;
}

int main(void) {
  build_chat_file();
  test_aborts();
  scenario_orders();
  scenario_seek(QA_Q3_BASE_PRODUCT_BASEQ3);
  scenario_seek(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  scenario_team(QA_Q3_BASE_PRODUCT_BASEQ3, 4);
  scenario_team(QA_Q3_BASE_PRODUCT_MISSIONPACK, 4);
  scenario_team(QA_Q3_BASE_PRODUCT_MISSIONPACK, 5);
  scenario_team(QA_Q3_BASE_PRODUCT_MISSIONPACK, 6);
  scenario_team(QA_Q3_BASE_PRODUCT_MISSIONPACK, 7);
  scenario_team(QA_Q3_BASE_PRODUCT_BASEQ3, 3);
  scenario_commands(QA_Q3_BASE_PRODUCT_BASEQ3);
  scenario_commands(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  scenario_chat(QA_Q3_BASE_PRODUCT_BASEQ3);
  scenario_chat(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  scenario_input();
  scenario_arsenal();
  scenario_main();
  size_t want = sizeof expected / sizeof expected[0];
  size_t mismatches = 0;
  for (size_t i = 0; i < want || i < line_count; i++) {
    const char *have = i < line_count ? lines[i] : "<missing>";
    const char *need = i < want ? expected[i] : "<extra>";
    checks++;
    if (strcmp(have, need) != 0) {
      failures++;
      if (mismatches++ < 25)
        fprintf(stderr, "line %zu\n  have: %s\n  want: %s\n", i + 1, have, need);
    }
  }
  for (size_t i = 0; i < line_count; i++)
    free(lines[i]);
  free(lines);
  printf("test_q3_command_team: %d/%d checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
