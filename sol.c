/**********************************************************************/
/*                                                                    */
/* File:                                                              */
/*   sol.c                                                            */
/*                                                                    */
/* Description:                                                       */
/*   Solve the Solitaire game.                                        */ 
/*                                                                    */
/* Decisions:                                                         */
/*   3.5 Mb of RAM is used                                            */
/*                                                                    */
/* History:                                                           */
/*   1996-05-19  A. Bosse     , created.                              */
/*                                                                    */
/**********************************************************************/

/******************************/
/*  Include files             */
/******************************/

#include <stdio.h>
#include <stdlib.h>

/******************************/
/*  External definitions      */
/******************************/

/******************************/
/*  Constant definitions      */
/******************************/

#define BOARD        33
#define PINS         32
#define SL           PINS

/* stuff based on 12 Mb RAM and 8 byte STATEs */
#define PINS_LEFT    21
#define PRIME_1      1499683
#define PRIME_2      1499681

/* stuff based on 3.5 Mb RAM and 8 byte STATEs */
//#define PINS_LEFT    22
//#define PRIME_1      383683
//#define PRIME_2      383681

#define HASH_SIZ     PRIME_1
#define HASH_HIST    22
 
/******************************/
/*  Type definitions          */
/******************************/

typedef struct state
{ unsigned int    id;
  unsigned short  slack;
  unsigned short  pins;
} STATE;

typedef struct update
{ int             allowed;
  int             rank;
  unsigned int    next_id;
  unsigned int    mask_id;
  unsigned short  next_slack;
  unsigned short  mask_slack;
} UPDATE;

/******************************/
/*  Forward definitions       */
/******************************/

static int  solve(STATE *state_p, int phase);

/******************************/
/*  Global data declarations  */
/******************************/

static UPDATE  table[BOARD * 4];
static int     move[BOARD];
static STATE   move_state[BOARD];
static STATE   solution_move_state[BOARD];
static STATE   rotated_start;
static STATE   rotated_final, real_final;
static STATE   start_state = { 0xffffffff, 0, PINS };
static STATE   final_state = { 0x00000000, 1, 1 };

static STATE   hash[HASH_SIZ];
static int     hash_added[HASH_HIST];
static int     hash_added_overflow;
static int     hash_added_total;
static int     hash_match[HASH_HIST];
static int     hash_match_overflow;
static int     hash_percent;
static int     hash_check_point;

/******************************/
/*  Global data declarations  */
/******************************/

/******************************/
/*  Macro definitions         */
/******************************/

#define HASH_1(k)         ((k) % PRIME_1)
#define HASH_2(k)         ((unsigned int)1 + (k) % PRIME_2)
#define DOUBLE_HASH(k,i)  (HASH_1(HASH_1(k) + (i) * HASH_2(k)))
#define SH(p,n)           (((p)->id & ((unsigned int)1 << (n))) ? '*' : '.')
#define SH_SL(p)          (((p)->slack & 1) ? '*' : '.')

/******************************/
/*  Local operations          */
/******************************/

/**********************************************************************/
/*                                                                    */
/* Operation: hash_statistics                                         */
/*                                                                    */
/* Abstract : Display information about hash table usage.             */
/* Returns  : -                                                       */
/* In       : verbose  full statistics or table usage only            */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static void
hash_statistics(int verbose)
{ int  i;

  if (verbose)
  { for (i = 0; i < HASH_HIST; i++)
    { printf("depth %2d : added %9d, match %9d\n",
        i, hash_added[i], hash_match[i]);
    }
    printf("depth %2d+: added %9d, match %9d\n",
      i, hash_added_overflow, hash_match_overflow);
  }
  printf("Hash table %d%c full.\n",
    hash_added_total * 100 / HASH_SIZ, '%');

  return;
}

/**********************************************************************/
/*                                                                    */
/* Operation: hash_init                                               */
/*                                                                    */
/* Abstract : Initialize all hash table variables and statistics.     */
/* Returns  : -                                                       */
/* In       : -                                                       */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static void
hash_init(void)
{ int  i;

  for (i = 0; i < HASH_SIZ; i++)
  { hash[i].pins      = 0;
    hash[i].id        = 0;
    hash[i].slack     = 0;
  }
  for (i = 0; i < HASH_HIST; i++)
  { hash_added[i]     = 0;
    hash_match[i]     = 0;
  }
  hash_added_overflow = 0;
  hash_added_total    = 0;
  hash_match_overflow = 0;
  hash_percent        = 10;
  hash_check_point    = HASH_SIZ * 10 / 100;

  return;
}

/**********************************************************************/
/*                                                                    */
/* Operation: rotate_state                                            */
/*                                                                    */
/* Abstract : Rotate state such that id is maximal (in case of        */
/*            rotate_count is -1) or rotate 'rotate_count' times.     */
/* Returns  : rotated  how much rotations are done (clockwise)        */
/* In       : actual_p      origin state being rotated                */
/*            rotate_count  #of rotations or (if -1) do best rotation */
/* In/Out   : -                                                       */
/* Out      : rotate_p      resulting state after rotation            */
/* Pre      : actual_p and rotate_p must point to proper states       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static int
rotate_state(STATE *actual_p, STATE *rotate_p, int rotate_count)
{ unsigned int   rotate_id;
  int            rotated = 0;
  unsigned char  *act    = (unsigned char*)&actual_p->id;
  unsigned char  *rot    = (unsigned char*)&rotate_id;

  *rotate_p = *actual_p;

  if (rotate_count == -1)
  { rot[0]=act[3]; rot[1]=act[0]; rot[2]=act[1]; rot[3]=act[2]; 
    if (rotate_id > rotate_p->id)
    { rotate_p->id = rotate_id;
      rotated = 1;
    }
    rot[0]=act[2]; rot[1]=act[3]; rot[2]=act[0]; rot[3]=act[1]; 
    if (rotate_id > rotate_p->id)
    { rotate_p->id = rotate_id;
      rotated = 2;
    }
    rot[0]=act[1]; rot[1]=act[2]; rot[2]=act[3]; rot[3]=act[0]; 
    if (rotate_id > rotate_p->id)
    { rotate_p->id = rotate_id;
      rotated = 3;
    }
  }
  else
  { if (rotate_count == 1)
    { rot[0]=act[3]; rot[1]=act[0]; rot[2]=act[1]; rot[3]=act[2]; 
      rotate_p->id = rotate_id;
      rotated = 1;
    }
    if (rotate_count == 2)
    { rot[0]=act[2]; rot[1]=act[3]; rot[2]=act[0]; rot[3]=act[1]; 
      rotate_p->id = rotate_id;
      rotated = 2;
    }
    if (rotate_count == 3)
    { rot[0]=act[1]; rot[1]=act[2]; rot[2]=act[3]; rot[3]=act[0]; 
      rotate_p->id = rotate_id;
      rotated = 3;
    }
  }

  return rotated;
}

/**********************************************************************/
/*                                                                    */
/* Operation: hash_add                                                */
/*                                                                    */
/* Abstract : Add a state to the hash table.                          */
/* Returns  : 0 if added, 1 if already in table or -1 if table full   */
/* In       : actual_p  pointer to state to add                       */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : actual_p must not be a null pointer                     */
/* Post     : hash statistics are updated if the state is added       */
/*            this statistics are displayed regularly                 */
/*                                                                    */
/**********************************************************************/

static int
hash_add(STATE *actual_p)
{ int    ret_val = -1;
  int    i;
  int    entry;
  STATE  rotate;

  rotate_state(actual_p, &rotate, -1);

  for (i = 0; i < HASH_SIZ; i++)
  { entry = DOUBLE_HASH(rotate.id, i);
    if (hash[entry].id == 0)
    { hash[entry] = rotate;

      if (i < HASH_HIST) { hash_added[i]++; } else { hash_added_overflow++; }
      if (++hash_added_total > hash_check_point)
      { hash_statistics(0);
        hash_percent += 10;
        hash_check_point = HASH_SIZ * hash_percent / 100;
      }

      ret_val = 0;
      break;
    }
    if (hash[entry].id == rotate.id &&
      (hash[entry].slack & 1) == (rotate.slack & 1))
    {
      if (i < HASH_HIST) { hash_match[i]++; } else { hash_match_overflow++; }

      ret_val = 1;
      break;
    }
  }

  if (ret_val == -1) { hash_match_overflow++; }

  return ret_val;
}

/**********************************************************************/
/*                                                                    */
/* Operation: hash_search                                             */
/*                                                                    */
/* Abstract : Search a state in the hash table.                       */
/* Returns  : 0 if found or -1 if not                                 */
/* In       : actual_p  pointer to state to search                    */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : actual_p must not be a null pointer                     */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static int
hash_search(STATE *actual_p)
{ int    ret_val = -1;
  int    i;
  int    entry;
  STATE  rotate;

  rotate_state(actual_p, &rotate, -1);

  for (i = 0; i < HASH_SIZ; i++)
  { entry = DOUBLE_HASH(rotate.id, i);
    if (hash[entry].id == 0)
    { break;
    }
    if (hash[entry].id == rotate.id &&
      (hash[entry].slack & 1) == (rotate.slack & 1))
    { ret_val = 0;
      break;
    }
  }

  return ret_val;
}

/**********************************************************************/
/*                                                                    */
/* Operation: show_state                                              */
/*                                                                    */
/* Abstract : Display state as playfield.                             */
/* Returns  : -                                                       */
/* In       : p  pointer to state to display.                         */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static void
show_state(STATE *p)
{
  printf("    %c %c %c\n"
         "    %c %c %c\n"
         "%c %c %c %c %c %c %c\n"
         "%c %c %c %c %c %c %c\n"
         "%c %c %c %c %c %c %c\n"
         "    %c %c %c\n"
         "    %c %c %c\n\n",
                        SH(p,0),  SH(p,1),  SH(p,2),
                        SH(p,3),  SH(p,4),  SH(p,5),
    SH(p,26), SH(p,29), SH(p,6),  SH(p,7),  SH(p,14), SH(p,11), SH(p,8),
    SH(p,25), SH(p,28), SH(p,31), SH_SL(p), SH(p,15), SH(p,12), SH(p,9),
    SH(p,24), SH(p,27), SH(p,30), SH(p,23), SH(p,22), SH(p,13), SH(p,10),
                        SH(p,21), SH(p,20), SH(p,19),
                        SH(p,18), SH(p,17), SH(p,16));

  return;
}

/**********************************************************************/
/*                                                                    */
/* Operation: initialize_entry                                        */
/*                                                                    */
/* Abstract : Initialize entry in table. Each entry indicates a       */
/*            possible move. Bitmaps are filled; they are used to     */
/*            determine the result of this move within a given state. */
/* Returns  : -                                                       */
/* In       : pinh  the actual board position (pinhole) [0..BOARD-1]  */
/*            idx   indicated the move direction [0..3]               */
/*            next  pin being jumped over when the move is done       */
/*            nxt2  pin jumping to the pinhole (pinh)                 */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static void
initialize_entry(int pinh, int idx, int next, int nxt2)
{
  if (next == -1 || nxt2 == -1)
  { table[4*pinh + idx].allowed =  0;
  }
  else
  { table[4*pinh + idx].allowed =  1;
    table[4*pinh + idx].rank    =  4*pinh + idx;
    
    table[4*pinh + idx].next_id    =  (next == SL) ? 0 : (1 << next);
    table[4*pinh + idx].next_slack =  (((next == SL) ? 1 : 0) & 1);
    table[4*pinh + idx].next_id    |= (nxt2 == SL) ? 0 : (1 << nxt2);
    table[4*pinh + idx].next_slack |= (((nxt2 == SL) ? 1 : 0) & 1);

    table[4*pinh + idx].mask_id    =  (pinh == SL) ? 0 : (1 << pinh);
    table[4*pinh + idx].mask_slack =  (((pinh == SL) ? 1 : 0) & 1);
    table[4*pinh + idx].mask_id    |= table[4*pinh + idx].next_id;
    table[4*pinh + idx].mask_slack |= ((table[4*pinh + idx].next_slack) & 1);
  }

  return;
}

/**********************************************************************/
/*                                                                    */
/* Operation: initialize_table                                        */
/*                                                                    */
/* Abstract : Fill table with all possible moves.                     */
/*            Identity all pinholes and initialize each move (4 ones) */
/*            possible jumping to this pinhole.                       */
/* Returns  : -                                                       */
/* In       : -                                                       */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : The ordering is done in such a way that 'rotate_state'  */
/*            (see above) is done at 'byte' level.                    */
/*                                                                    */
/**********************************************************************/

static void
initialize_table(void)
{        int  i;
  static int  arr[121] = 
  { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1,  0,  1,  2, -1, -1, -1, -1,
    -1, -1, -1, -1,  3,  4,  5, -1, -1, -1, -1,
    -1, -1, 26, 29,  6,  7, 14, 11,  8, -1, -1,
    -1, -1, 25, 28, 31, SL, 15, 12,  9, -1, -1,
    -1, -1, 24, 27, 30, 23, 22, 13, 10, -1, -1,
    -1, -1, -1, -1, 21, 20, 19, -1, -1, -1, -1,
    -1, -1, -1, -1, 18, 17, 16, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
  };

  for (i = 0; i < 121; i++)
  { if (arr[i] != -1)
    { initialize_entry(arr[i], 0, arr[i - 11], arr[i - 22]);
      initialize_entry(arr[i], 1, arr[i +  1], arr[i +  2]);
      initialize_entry(arr[i], 2, arr[i + 11], arr[i + 22]);
      initialize_entry(arr[i], 3, arr[i -  1], arr[i -  2]);
    }
  }

  return;
}

/**********************************************************************/
/*                                                                    */
/* Operation: show_solution                                           */
/*                                                                    */
/* Abstract : Display states of a complete solution.                  */
/*            The user is being asked to give input after each state. */
/* Returns  : -                                                       */
/* In       : -                                                       */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static void
show_solution(void)
{ int  i;
  int  tmp;

  solution_move_state[PINS] = start_state;
  solution_move_state[1]    = final_state;

  printf("solution:\n");
  for (i = PINS; i >= 1; i--)
  { show_state(&solution_move_state[i]);
    printf("<return> to continue...");
    tmp = getchar();
  }
  
  return;
}

/**********************************************************************/
/*                                                                    */
/* Operation: check_phase3_results                                    */
/*                                                                    */
/* Abstract : Determine if trailing or leading part is encountered.   */
/* Returns  : 0 if not done or 1 if done (both parts are identified)  */
/* In       : state_p  pointer to state to check                      */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : If done then the first part and last part of the        */
/*            solution is stored.                                     */
/*                                                                    */
/**********************************************************************/

static int
check_phase3_result(STATE *actual_p)
{        STATE  rotate;
         int    i, rot, rot2;
  static int    phase3_result = 0;

  rot = rotate_state(actual_p, &rotate, -1);
  if (rotate.id == rotated_start.id &&
    (rotate.slack & 1) == (rotated_start.slack & 1))
  { for (i = PINS; i > PINS_LEFT; i--)
    { rotate_state(&move_state[i], &rotate, rot);
      solution_move_state[i - 1] = rotate;
    }
    /* heading part stored */
    phase3_result |= 1;
  }
  if (rotate.id == rotated_final.id &&
    (rotate.slack & 1) == (rotated_final.slack & 1))
  { rot2 = rotate_state(&real_final, &rotate, -1);
    rot  = (rot + 4 - rot2) % 4;
    for (i = PINS; i > PINS_LEFT; i--)
    { rotate_state(&move_state[i], &rotate, rot);
      rotate.id    = ~rotate.id;
      rotate.slack = ((~rotate.slack) & 1);
      rotate.pins  = BOARD - rotate.pins;
      solution_move_state[BOARD - i + 1] = rotate;
    }
    /* trailing part stored */
    phase3_result |= 2;
  }

  return (phase3_result == 3) ? 1 : 0;
}

/**********************************************************************/
/*                                                                    */
/* Operation: check_part2_result                                      */
/*                                                                    */
/* Abstract : determine if trailing frontier is encountered.          */
/* Returns  : 0 if not done or 1 if done                              */
/* In       : state_p  pointer to state being checked                 */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : If done then the middle part of the solution is stored. */
/*                                                                    */
/**********************************************************************/

static int
check_phase2_result(STATE *actual_p)
{ int    done = 0;
  STATE  rotate;
  int    i;

  rotate.id    = ~actual_p->id;
  rotate.slack = ((~actual_p->slack) & 1);
  rotate.pins  = PINS_LEFT;

  if (hash_search(&rotate) == 0)
  { printf(" found.\n");
    real_final = rotate;
    rotate_state(&real_final, &rotated_final, -1);
    for (i = PINS_LEFT; i >= BOARD - PINS_LEFT; i--)
    { solution_move_state[i - 1] = move_state[i];
    } 
    /* middle part stored */
    done = 1;
  }

  return done;
}

/**********************************************************************/
/*                                                                    */
/* Operation: solve_pin                                               */
/*                                                                    */
/* Abstract : Solve state starting with a move to specific pinhole.   */
/* Returns  : 0 if not done of 1 if done                              */
/* In       : actual_p  pointer to state being solved                 */
/*            pin_4     first move (of 4) being possible              */
/*            phase     determines stage of the solution process      */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static int
solve_pin(STATE *actual_p, int pin_4, int phase)
{ int     done = 0;
  STATE   new_state;
  UPDATE  *update_p, *last_update_p;
  int     j;

  for (j = 0; j < 4 && !done; j++)
  { update_p = &table[pin_4 + j];

    if (update_p->allowed &&
      (actual_p->id & update_p->next_id) == update_p->next_id &&
      (actual_p->slack & update_p->next_slack & 1) == update_p->next_slack)
    {
      /* traversal optimization */
      if (actual_p->pins < PINS_LEFT)
      { last_update_p = &table[move[actual_p->pins + 1]];
        if (update_p->rank < last_update_p->rank &&
          (update_p->mask_id & last_update_p->mask_id) == 0 &&
          (update_p->mask_slack & last_update_p->mask_slack & 1) == 0)
        { /* no collision -> already done */
          break;
        }
      }
      new_state.id    = actual_p->id ^ update_p->mask_id;
      new_state.slack = ((actual_p->slack ^ update_p->mask_slack) & 1);
      new_state.pins  = actual_p->pins  - 1;

      move[actual_p->pins] = pin_4 + j;
      move_state[actual_p->pins] = new_state;
      done = solve(&new_state, phase);
    }
  }
 
  return done;
}

/**********************************************************************/
/*                                                                    */
/* Operation: solve_sub                                               */
/*                                                                    */
/* Abstract : Solve all following states possible from the actual.    */
/* Returns  : 0 if not done or 1 if done                              */
/* In       : actual_p  pointer to state being solved                 */
/*            phase     determines stage of the solution process      */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

static int
solve_sub(STATE *actual_p, int phase)
{ int           done = 0;
  int           pin;
  unsigned int  bit;

  for (pin = 0, bit = 1; pin < PINS && !done; pin++, bit <<= 1)
  { if ((actual_p->id & bit) == 0)
    { done = solve_pin(actual_p, 4 * pin, phase);
    }
  }
  if (!done && (actual_p->slack & 1) == 0)
  { done = solve_pin(actual_p, 4 * SL, phase);
  }

  return done;
}

/**********************************************************************/
/*                                                                    */
/* Operation: solve                                                   */
/*                                                                    */
/* Abstract : Solve state for a specific phase.                       */
/* Returns  : 0 if not done or 1 if done                              */
/* In       : actual_p  pointer to state being solved                 */
/*            phase     determines stage of the solution process      */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : If the hash table is full the program exits.            */
/*                                                                    */
/**********************************************************************/

static int
solve(STATE *actual_p, int phase)
{ int  done = 0;
  int  hash_result;

  if (phase == 2)
  { if (actual_p->pins == BOARD - PINS_LEFT)
    { done = check_phase2_result(actual_p);
    }
    else
    { done = solve_sub(actual_p, phase);
    }
  }
  else
  { if ((hash_result = hash_add(actual_p)) == 0)
    { if (actual_p->pins == PINS_LEFT)
      { if (phase == 3)
        { done = check_phase3_result(actual_p);
        }
      }
      else
      { done = solve_sub(actual_p, phase);
      }
    }
    else
    { if (hash_result == -1)
      { hash_statistics(1);
        exit(1);
      }
    }
  }
 
  return done;
}

/******************************/
/*  Global operations         */
/******************************/

/**********************************************************************/
/*                                                                    */
/* Operation: main                                                    */
/*                                                                    */
/* Abstract : Solve the solitaire problem.                            */
/*            The solotion process is split up in three phases:       */
/*            Phase 1 sets up a hash table in which all possible      */
/*                    states are stored at depth PINS_LEFT.           */
/*                    Note that the states at depth BOARD - PINS_LEFT */
/*                    are the same states, but reversed.              */
/*            Phase 2 finds a path between depth PINS_LEFT (the       */
/*                    heading frontier) and BOARD - PINS_LEFT (the    */
/*                    trailing frontier). The clue is to match a      */
/*                    reversed state of the depth first traversal     */
/*                    with a state of the trailing frontier.          */
/*                    This is done using the hash table.              */
/*            Phase 3 again creates the hash table but keeps in mind  */
/*                    that the two states indicating the head and     */
/*                    (reversed) tail (of the path found in Phase 2)  */
/*                    must be found for their parts of the solution.  */
/*            Finally the solution is obtained when the result of     */
/*            phase 2 is combined with the result of phase 3.         */
/* Returns  : 0                                                       */
/* In       : -                                                       */
/* In/Out   : -                                                       */
/* Out      : -                                                       */
/* Pre      : -                                                       */
/* Post     : -                                                       */
/*                                                                    */
/**********************************************************************/

int
main(void)
{ int  i, done;

  initialize_table();

  printf("Phase 1: full search from %d to %d.\n", PINS, PINS_LEFT);
  hash_init();
  solve(&start_state, 1);

  printf("Phase 2: traversal from %d to %d.\n", PINS_LEFT, BOARD - PINS_LEFT);
  printf("searching..."); fflush(stdout);
  for (i = done = 0; i < HASH_SIZ && !done; i++)
  { if (hash[i].pins == PINS_LEFT)
    { rotated_start = hash[i];
      done = solve(&rotated_start, 2);
    }
  }

  printf("Phase 3: locate heading and trailing parts.\n");
  hash_init();
  solve(&start_state, 3);

  show_solution();

  return 0;
}
