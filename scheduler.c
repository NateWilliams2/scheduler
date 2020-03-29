// Nate Williams and Isa Nation
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>
#include <stdbool.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// This is the ready and running and completed states
#define READY 0
#define RUNNING 1
#define COMPLETE 2
#define SLEEPING 3
#define WAIT 4
#define WINPUT 5

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;

  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  int state;
  //   b. If the task is sleeping, when should it wake up?
  size_t wakeup;
  //   c. If the task is waiting for another task, which task is it waiting for?
  task_t blocker;
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
  int input;
} task_info_t;

int current_task = 0; //< The handle of the currently-executing task
int num_tasks = 1;    //< The number of tasks created so far
task_info_t tasks[MAX_TASKS]; //< Information for every task

/**
* This function is the heart of the scheduler. When called, it loops through the
* list of tasks and exits when it identifies and runs the next valid task.
**/
void schedule(){
  int next_task = current_task;
  bool isLooping = TRUE; // Flag to exit loop
  int num_completes = 0; // Keeps track of number of COMPLETE states
  while(isLooping && num_completes <= num_tasks)
  // Loops unless all tasks are in the COMPLETE state
  {
    next_task = schedler_increment(next_task);
    switch (tasks[next_task].state)
    {
      case READY: // task is in ready state
        // runs the task
        isLooping = FALSE;
        num_completes = 0;
        break;
      case RUNNING: // task is in running state
        perror("State is somehow still running");
        isLooping = TRUE;
        num_completes = 0;
        break;
      case COMPLETE: // task is in complete state;
        isLooping = TRUE;
        num_completes++;
        break;
      case SLEEPING: // task is in sleeping state
        if(time_ms() >= (tasks[next_task].wakeup))
          isLooping = FALSE;
        else isLooping = TRUE;
        num_completes = 0;
        break;
      case WAIT: // task is in wait state
        if(tasks[tasks[next_task].blocker].state == COMPLETE)
          isLooping = FALSE;
        else isLooping = TRUE;
        num_completes = 0;
        break;
      case WINPUT: // task is in wait-input state
        ; //apparently, you can't declare a variable on the first line of a switch case
        int input = getch();
        if(input == ERR)
        isLooping = TRUE;
        else{
          isLooping = FALSE;
          tasks[next_task].input = input;
        }
        num_completes = 0;
        break;
      default:
        perror("State is invalid");
        isLooping = FALSE;
        num_completes = 0;
        break;
    }
  }
  if(num_completes <= num_tasks){
    // When loop exits, check if all tasks are in COMPLETE state
    int tmp_task = current_task;
    current_task = next_task;
    tasks[current_task].state = RUNNING; // Update state of task to run
    // Swap contexts so that current_task is now running
    swapcontext(&((tasks[tmp_task]).context), &((tasks[current_task]).context));
  }
}


/**
 * Initialize the scheduler. Simply calls the schedule() function to transfer control
 */
void scheduler_init() {
  schedule();
}


/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  // Handles the end of a task's execution here'
  tasks[current_task].state = COMPLETE;
  schedule();
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;

  // Set the task handle to this index, since task_t is just an int
  *handle = index;
;
  // We're going to make  size_t final = time_ms() + ms; two contexts: one to run the task, and one that runs at the end of the task so we can clean up. Start with the second

  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);

  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;

  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);

  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);

  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;

  // Now set the uc_link field, which sets things up so our task will go to the exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;

  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
}

/**
 * Wait for a taskif(next_task >= num_tasks)
      next_task = 0;
    else next_task++; to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  tasks[current_task].state = WAIT;
  tasks[current_task].blocker = handle; // Set task's state and blocker handle
  schedule();
}

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 *
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  size_t final = time_ms() + ms;
  if(ms > 0){
    tasks[current_task].wakeup = final;
    tasks[current_task].state = SLEEPING; // Set task's state and wakup time
  }
  schedule();
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // right now the scheduler will be completely blocked.
    tasks[current_task].state = WINPUT;
    schedule(); // Scheduler calls getch() and updates input field
    return tasks[current_task].input;
}

/**
 * Given the current task index, gets the next task.
 * Simply increments task index unless we are at the end of the array
 *  \param next_task the current task
 *
 * \returns the incremeted task
 */
int schedler_increment(int next_task){
  next_task++;
  if(next_task >= num_tasks)
      next_task = 0;
  return next_task;
}
