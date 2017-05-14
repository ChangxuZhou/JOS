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
static int prev = -1;

void sched_yield(void)
{
    static int i = 0;
    while (1) {
        i++;
        i = i % NENV;
        if (envs[i].env_status == ENV_RUNNABLE) {
            /*if (i != prev) {
                u_int j = 0xFFF;
                while (j--);
                printf("\n************PID: %d : pc start @ [%8x]\n", i, envs[i].env_tf.pc);
                prev = i;
                j = 0xFFF;
                while (j--);
            }*/

            env_run(&envs[i]);
            return;
        }
    }
}
