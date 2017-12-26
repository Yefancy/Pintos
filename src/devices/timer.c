#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void)
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void)
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1))
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void)
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then)
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
  //int64_t start = timer_ticks ();//获取timerticks记录的当前tick
  ASSERT (intr_get_level () == INTR_ON);//断言可以中断
  //原意：在ticks内 不断将在跑线程拉入就绪队列
  /*while (timer_elapsed (start) < ticks)
    thread_yield ();*/
  enum intr_level old_level = intr_disable ();//保证过程不被中断 操作的原子性
  if(ticks > 0)//休眠时间有意义
  {
    thread_current()->blocked_ticks = ticks;//设置阻塞时间
    thread_block();//thread提供的 block函数
  }
  intr_set_level (old_level);
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms)
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us)
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns)
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms)
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us)
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns)
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void)
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. ticks中断时调用*/
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick ();//线程tick处理
  thread_foreach((thread_action_func *)&thread_revise_blocked_ticks ,1);//所有线程 遍历调用函数
  if(thread_mlfqs)
  {
    struct thread *t = thread_current();
    if(t != idle_thread)
    {
      t->recent_cpu = MU_ADD_MIX (t->recent_cpu, 1);
    }
    /* Convert NUM/DENOM seconds into timer ticks, rounding down.

          (NUM / DENOM) s
       ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
       1 s / TIMER_FREQ ticks pintos中TIMER_FREQ为1秒
    */
    if (ticks % TIMER_FREQ == 0)
    {
      size_t ready_threads = list_size(&ready_list);
      if(t !=idle_thread)//如果非空闲线程 那么已就绪的线程数（课直接运行的线程数）+1
        ready_threads++:

      //更新load_avg值 （59/60）* load_avg +（1/60）* ready_threads
      load_avg = MU_ADD (MU_DIV_MIX ( MU_MULT_MIX (load_avg, 59), 60), MU_DIV_MIX (MU_CONST (ready_threads), 60));

      //更新recent_cpu（每1秒 TIMER_FREQ） （2 * load_avg）/（2 * load_avg + 1）* recent_cpu + nice
      struct thread *tmp_thread;
      /*An empty list looks like this:

                         +------+     +------+
                     <---| head |<--->| tail |--->
                         +------+     +------+

      A list with two elements in it looks like this:

           +------+     +-------+     +-------+     +------+
       <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
           +------+     +-------+     +-------+     +------+
           */
      //遍历所有线程并更新recent_cpu  list存储方式见上
      for(struct list_elem *head = list_head(&all_list); head != list_end(&all_list); head=list_next(head))
      {
        tmp_thread = list_entry(head, struct thread, allelem);
        if(tmp_thread !=idle_thread)
        {
          tmp_thread->recent_cpu = MU_ADD_MIX (MU_MULT (MU_DIV (MU_MULT_MIX (load_avg, 2), MU_ADD_MIX (MU_MULT_MIX (load_avg, 2), 1)), tmp_thread->recent_cpu), tmp_thread->nice);
          tmp_thread->priority = MU_INT_PART (MU_SUB_MIX (MU_SUB (MU_CONST (PRI_MAX), MU_DIV_MIX (tmp_thread->recent_cpu, 4)), 2 * tmp_thread->nice));
        }
      }
    }
    else if (ticks % 4 == 3 && t != idle_thread)
      //更新优先级 PRI_MAX- （recent_cpu / 4） - （nice * 2）
      t->priority = MU_INT_PART (MU_SUB_MIX (MU_SUB (MU_CONST (PRI_MAX), MU_DIV_MIX (t->recent_cpu, 4)), 2 * t->nice));
  }
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops)
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops)
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom)
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.

        (NUM / DENOM) s
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */
      timer_sleep (ticks);
    }
  else
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom);
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}
