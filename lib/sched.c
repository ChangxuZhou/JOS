#include <env.h>
#include <pmap.h>
#include <printf.h>

/* Overview:
 *  Implement simple round-robin scheduling.
 *  Search through 'envs' for a runnable environment ,
 *  in circular fashion statrting after the previously running env,
 *  and switch to the first such environment found.
 *
 * Hints:
 *  The variable which is for counting should be defined as 'static'.
 */
void sched_yield(void)
{
    static int i = 0;
    while (1) {
        i++;
        i = i % NENV;
        if (envs[i].env_status == ENV_RUNNABLE) {
            env_run(&envs[i]);
            return;
        }
    }
}
