/*******************************************************************************
 * This file is part of SWIFT.
 * Coypright (c) 2012 Pedro Gonnet (pedro.gonnet@durham.ac.uk)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

/* Config parameters. */
#include "../config.h"

/* Some standard headers. */
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MPI headers. */
#ifdef WITH_MPI
#include <mpi.h>
#endif

/* This object's header. */
#include "scheduler.h"

/* Local headers. */
#include "atomic.h"
#include "const.h"
#include "cycle.h"
#include "error.h"
#include "intrinsics.h"
#include "kernel.h"
#include "timers.h"

/**
 * @brief Add an unlock_task to the given task.
 *
 * @param s The #scheduler.
 * @param ta The unlocking #task.
 * @param tb The #task that will be unlocked.
 */

void scheduler_addunlock(struct scheduler *s, struct task *ta,
                         struct task *tb) {

  /* Main loop. */
  while (1) {

    /* Follow the links. */
    while (ta->nr_unlock_tasks == task_maxunlock + 1)
      ta = ta->unlock_tasks[task_maxunlock];

    /* Get the index of the next free task. */
    int ind = atomic_inc(&ta->nr_unlock_tasks);

    /* Is there room in this task? */
    if (ind < task_maxunlock) {
      ta->unlock_tasks[ind] = tb;
      break;
    }

    /* Otherwise, generate a link task. */
    else {

      /* Only one thread should have to do this. */
      if (ind == task_maxunlock) {
        ta->unlock_tasks[task_maxunlock] =
            scheduler_addtask(s, task_type_link, task_subtype_none, ta->flags,
                              0, ta->ci, ta->cj, 0);
        ta->unlock_tasks[task_maxunlock]->implicit = 1;
      }

      /* Otherwise, reduce the count. */
      else
        atomic_dec(&ta->nr_unlock_tasks);
    }
  }
}

/**
 * @brief Split tasks that may be too large.
 *
 * @param s The #scheduler we are working in.
 */

void scheduler_splittasks(struct scheduler *s) {

  int j, k, ind, sid, tid = 0, redo;
  struct cell *ci, *cj;
  double hi, hj, shift[3];
  struct task *t, *t_old;
  // float dt_step = s->dt_step;
  int pts[7][8] = {{-1, 12, 10, 9, 4, 3, 1, 0},
                   {-1, -1, 11, 10, 5, 4, 2, 1},
                   {-1, -1, -1, 12, 7, 6, 4, 3},
                   {-1, -1, -1, -1, 8, 7, 5, 4},
                   {-1, -1, -1, -1, -1, 12, 10, 9},
                   {-1, -1, -1, -1, -1, -1, 11, 10},
                   {-1, -1, -1, -1, -1, -1, -1, 12}};
  float sid_scale[13] = {0.1897, 0.4025, 0.1897, 0.4025, 0.5788, 0.4025, 0.1897,
                         0.4025, 0.1897, 0.4025, 0.5788, 0.4025, 0.5788};

  /* Loop through the tasks... */
  redo = 0;
  t_old = t = NULL;
  while (1) {

    /* Get a pointer on the task. */
    if (redo) {
      redo = 0;
      t = t_old;
    } else {
      if ((ind = atomic_inc(&tid)) < s->nr_tasks)
        t_old = t = &s->tasks[s->tasks_ind[ind]];
      else
        break;
    }

    /* Empty task? */
    if (t->ci == NULL || (t->type == task_type_pair && t->cj == NULL)) {
      t->type = task_type_none;
      t->skip = 1;
      continue;
    }

    /* Non-local kick task? */
    if ((t->type == task_type_kick1 || t->type == task_type_kick2) &&
        t->ci->nodeID != s->nodeID) {
      t->type = task_type_none;
      t->skip = 1;
      continue;
    }

    /* Self-interaction? */
    if (t->type == task_type_self) {

      /* Get a handle on the cell involved. */
      ci = t->ci;

      /* Foreign task? */
      if (ci->nodeID != s->nodeID) {
        t->skip = 1;
        continue;
      }

      /* Is this cell even split? */
      if (ci->split) {

        /* Make a sub? */
        if (scheduler_dosub && ci->count < space_subsize / ci->count) {

          /* convert to a self-subtask. */
          t->type = task_type_sub;

        }

        /* Otherwise, make tasks explicitly. */
        else {

          /* Take a step back (we're going to recycle the current task)... */
          redo = 1;

          /* Add the self taks. */
          for (k = 0; ci->progeny[k] == NULL; k++)
            ;
          t->ci = ci->progeny[k];
          for (k += 1; k < 8; k++)
            if (ci->progeny[k] != NULL)
              scheduler_addtask(s, task_type_self, task_subtype_density, 0, 0,
                                ci->progeny[k], NULL, 0);

          /* Make a task for each pair of progeny. */
          for (j = 0; j < 8; j++)
            if (ci->progeny[j] != NULL)
              for (k = j + 1; k < 8; k++)
                if (ci->progeny[k] != NULL)
                  scheduler_addtask(s, task_type_pair, task_subtype_density,
                                    pts[j][k], 0, ci->progeny[j],
                                    ci->progeny[k], 0);
        }
      }

    }

    /* Pair interaction? */
    else if (t->type == task_type_pair) {

      /* Get a handle on the cells involved. */
      ci = t->ci;
      cj = t->cj;
      hi = ci->dmin;
      hj = cj->dmin;

      /* Foreign task? */
      if (ci->nodeID != s->nodeID && cj->nodeID != s->nodeID) {
        t->skip = 1;
        continue;
      }

      /* Get the sort ID, use space_getsid and not t->flags
         to make sure we get ci and cj swapped if needed. */
      sid = space_getsid(s->space, &ci, &cj, shift);

      /* Should this task be split-up? */
      if (ci->split && cj->split &&
          ci->h_max * kernel_gamma * space_stretch < hi / 2 &&
          cj->h_max * kernel_gamma * space_stretch < hj / 2) {

        /* Replace by a single sub-task? */
        if (scheduler_dosub &&
            ci->count * sid_scale[sid] < space_subsize / cj->count &&
            sid != 0 && sid != 2 && sid != 6 && sid != 8) {

          /* Make this task a sub task. */
          t->type = task_type_sub;

        }

        /* Otherwise, split it. */
        else {

          /* Take a step back (we're going to recycle the current task)... */
          redo = 1;

          /* For each different sorting type... */
          switch (sid) {

            case 0: /* (  1 ,  1 ,  1 ) */
              t->ci = ci->progeny[7];
              t->cj = cj->progeny[0];
              t->flags = 0;
              break;

            case 1: /* (  1 ,  1 ,  0 ) */
              t->ci = ci->progeny[6];
              t->cj = cj->progeny[0];
              t->flags = 1;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 1, 0,
                                    ci->progeny[7], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                    ci->progeny[6], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 2, 0,
                                    ci->progeny[7], cj->progeny[0], 1);
              break;

            case 2: /* (  1 ,  1 , -1 ) */
              t->ci = ci->progeny[6];
              t->cj = cj->progeny[1];
              t->flags = 2;
              t->tight = 1;
              break;

            case 3: /* (  1 ,  0 ,  1 ) */
              t->ci = ci->progeny[5];
              t->cj = cj->progeny[0];
              t->flags = 3;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 3, 0,
                                    ci->progeny[7], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                    ci->progeny[5], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 6, 0,
                                    ci->progeny[7], cj->progeny[0], 1);
              break;

            case 4: /* (  1 ,  0 ,  0 ) */
              t->ci = ci->progeny[4];
              t->cj = cj->progeny[0];
              t->flags = 4;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 5, 0,
                                    ci->progeny[5], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 7, 0,
                                    ci->progeny[6], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 8, 0,
                                    ci->progeny[7], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 3, 0,
                                    ci->progeny[4], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 4, 0,
                                    ci->progeny[5], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 6, 0,
                                    ci->progeny[6], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 7, 0,
                                    ci->progeny[7], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 1, 0,
                                    ci->progeny[4], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 2, 0,
                                    ci->progeny[5], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 4, 0,
                                    ci->progeny[6], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 5, 0,
                                    ci->progeny[7], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                    ci->progeny[4], cj->progeny[3], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 1, 0,
                                    ci->progeny[5], cj->progeny[3], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 3, 0,
                                    ci->progeny[6], cj->progeny[3], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 4, 0,
                                    ci->progeny[7], cj->progeny[3], 1);
              break;

            case 5: /* (  1 ,  0 , -1 ) */
              t->ci = ci->progeny[4];
              t->cj = cj->progeny[1];
              t->flags = 5;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 5, 0,
                                    ci->progeny[6], cj->progeny[3], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 2, 0,
                                    ci->progeny[4], cj->progeny[3], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 8, 0,
                                    ci->progeny[6], cj->progeny[1], 1);
              break;

            case 6: /* (  1 , -1 ,  1 ) */
              t->ci = ci->progeny[5];
              t->cj = cj->progeny[2];
              t->flags = 6;
              t->tight = 1;
              break;

            case 7: /* (  1 , -1 ,  0 ) */
              t->ci = ci->progeny[4];
              t->cj = cj->progeny[3];
              t->flags = 6;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 8, 0,
                                    ci->progeny[5], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 7, 0,
                                    ci->progeny[4], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 7, 0,
                                    ci->progeny[5], cj->progeny[3], 1);
              break;

            case 8: /* (  1 , -1 , -1 ) */
              t->ci = ci->progeny[4];
              t->cj = cj->progeny[3];
              t->flags = 8;
              t->tight = 1;
              break;

            case 9: /* (  0 ,  1 ,  1 ) */
              t->ci = ci->progeny[3];
              t->cj = cj->progeny[0];
              t->flags = 9;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 9, 0,
                                    ci->progeny[7], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                    ci->progeny[3], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 8, 0,
                                    ci->progeny[7], cj->progeny[0], 1);
              break;

            case 10: /* (  0 ,  1 ,  0 ) */
              t->ci = ci->progeny[2];
              t->cj = cj->progeny[0];
              t->flags = 10;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 11, 0,
                                    ci->progeny[3], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 7, 0,
                                    ci->progeny[6], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 6, 0,
                                    ci->progeny[7], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 9, 0,
                                    ci->progeny[2], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 10, 0,
                                    ci->progeny[3], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 8, 0,
                                    ci->progeny[6], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 7, 0,
                                    ci->progeny[7], cj->progeny[1], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 1, 0,
                                    ci->progeny[2], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 2, 0,
                                    ci->progeny[3], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 10, 0,
                                    ci->progeny[6], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 11, 0,
                                    ci->progeny[7], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                    ci->progeny[2], cj->progeny[5], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 1, 0,
                                    ci->progeny[3], cj->progeny[5], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 9, 0,
                                    ci->progeny[6], cj->progeny[5], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 10, 0,
                                    ci->progeny[7], cj->progeny[5], 1);
              break;

            case 11: /* (  0 ,  1 , -1 ) */
              t->ci = ci->progeny[2];
              t->cj = cj->progeny[1];
              t->flags = 11;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 11, 0,
                                    ci->progeny[6], cj->progeny[5], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 2, 0,
                                    ci->progeny[2], cj->progeny[5], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 6, 0,
                                    ci->progeny[6], cj->progeny[1], 1);
              break;

            case 12: /* (  0 ,  0 ,  1 ) */
              t->ci = ci->progeny[1];
              t->cj = cj->progeny[0];
              t->flags = 12;
              t->tight = 1;
              t = scheduler_addtask(s, task_type_pair, t->subtype, 11, 0,
                                    ci->progeny[3], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 5, 0,
                                    ci->progeny[5], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 2, 0,
                                    ci->progeny[7], cj->progeny[0], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 9, 0,
                                    ci->progeny[1], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 12, 0,
                                    ci->progeny[3], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 8, 0,
                                    ci->progeny[5], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 5, 0,
                                    ci->progeny[7], cj->progeny[2], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 3, 0,
                                    ci->progeny[1], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 6, 0,
                                    ci->progeny[3], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 12, 0,
                                    ci->progeny[5], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 11, 0,
                                    ci->progeny[7], cj->progeny[4], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                    ci->progeny[1], cj->progeny[6], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 3, 0,
                                    ci->progeny[3], cj->progeny[6], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 9, 0,
                                    ci->progeny[5], cj->progeny[6], 1);
              t = scheduler_addtask(s, task_type_pair, t->subtype, 12, 0,
                                    ci->progeny[7], cj->progeny[6], 1);
              break;
          }
        }

      } /* split this task? */

      /* Otherwise, break it up if it is too large? */
      else if (scheduler_doforcesplit && ci->split && cj->split &&
               (ci->count > space_maxsize / cj->count)) {

        // message( "force splitting pair with %i and %i parts." , ci->count ,
        // cj->count );

        /* Replace the current task. */
        t->type = task_type_none;

        for (j = 0; j < 8; j++)
          if (ci->progeny[j] != NULL)
            for (k = 0; k < 8; k++)
              if (cj->progeny[k] != NULL) {
                t = scheduler_addtask(s, task_type_pair, t->subtype, 0, 0,
                                      ci->progeny[j], cj->progeny[k], 0);
                t->flags = space_getsid(s->space, &t->ci, &t->cj, shift);
              }

      }

      /* Otherwise, if not spilt, stitch-up the sorting. */
      else {

        /* Create the sort for ci. */
        // lock_lock( &ci->lock );
        if (ci->sorts == NULL)
          ci->sorts =
              scheduler_addtask(s, task_type_sort, 0, 1 << sid, 0, ci, NULL, 0);
        else
          ci->sorts->flags |= (1 << sid);
        // lock_unlock_blind( &ci->lock );
        scheduler_addunlock(s, ci->sorts, t);

        /* Create the sort for cj. */
        // lock_lock( &cj->lock );
        if (cj->sorts == NULL)
          cj->sorts =
              scheduler_addtask(s, task_type_sort, 0, 1 << sid, 0, cj, NULL, 0);
        else
          cj->sorts->flags |= (1 << sid);
        // lock_unlock_blind( &cj->lock );
        scheduler_addunlock(s, cj->sorts, t);
      }

    } /* pair interaction? */

    /* Gravity interaction? */
    else if (t->type == task_type_grav_mm) {

      /* Get a handle on the cells involved. */
      ci = t->ci;
      cj = t->cj;

      /* Self-interaction? */
      if (cj == NULL) {

        /* Ignore this task if the cell has no gparts. */
        if (ci->gcount == 0) t->type = task_type_none;

        /* If the cell is split, recurse. */
        else if (ci->split) {

          /* Make a single sub-task? */
          if (scheduler_dosub && ci->count < space_subsize / ci->count) {

            t->type = task_type_sub;
            t->subtype = task_subtype_grav;

          }

          /* Otherwise, just split the task. */
          else {

            /* Split this task into tasks on its progeny. */
            t->type = task_type_none;
            for (j = 0; j < 8; j++)
              if (ci->progeny[j] != NULL && ci->progeny[j]->gcount > 0) {
                if (t->type == task_type_none) {
                  t->type = task_type_grav_mm;
                  t->ci = ci->progeny[j];
                  t->cj = NULL;
                } else
                  t = scheduler_addtask(s, task_type_grav_mm, task_subtype_none,
                                        0, 0, ci->progeny[j], NULL, 0);
                for (k = j + 1; k < 8; k++)
                  if (ci->progeny[k] != NULL && ci->progeny[k]->gcount > 0) {
                    if (t->type == task_type_none) {
                      t->type = task_type_grav_mm;
                      t->ci = ci->progeny[j];
                      t->cj = ci->progeny[k];
                    } else
                      t = scheduler_addtask(s, task_type_grav_mm,
                                            task_subtype_none, 0, 0,
                                            ci->progeny[j], ci->progeny[k], 0);
                  }
              }
            redo = (t->type != task_type_none);
          }

        }

        /* Otherwise, just make a pp task out of it. */
        else
          t->type = task_type_grav_pp;

      }

      /* Nope, pair. */
      else {

        /* Make a sub-task? */
        if (scheduler_dosub && ci->count < space_subsize / cj->count) {

          t->type = task_type_sub;
          t->subtype = task_subtype_grav;

        }

        /* Otherwise, split the task. */
        else {

          /* Get the opening angle theta. */
          float dx[3], theta;
          for (k = 0; k < 3; k++) {
            dx[k] = fabsf(ci->loc[k] - cj->loc[k]);
            if (s->space->periodic && dx[k] > 0.5 * s->space->dim[k])
              dx[k] = -dx[k] + s->space->dim[k];
            if (dx[k] > 0.0f) dx[k] -= ci->h[k];
          }
          theta =
              (dx[0] * dx[0] + dx[1] * dx[1] + dx[2] * dx[2]) /
              (ci->h[0] * ci->h[0] + ci->h[1] * ci->h[1] + ci->h[2] * ci->h[2]);

          /* Ignore this task if the cell has no gparts. */
          if (ci->gcount == 0 || cj->gcount == 0) t->type = task_type_none;

          /* Split the interacton? */
          else if (theta < const_theta_max * const_theta_max) {

            /* Are both ci and cj split? */
            if (ci->split && cj->split) {

              /* Split this task into tasks on its progeny. */
              t->type = task_type_none;
              for (j = 0; j < 8; j++)
                if (ci->progeny[j] != NULL && ci->progeny[j]->gcount > 0) {
                  for (k = 0; k < 8; k++)
                    if (cj->progeny[k] != NULL && cj->progeny[k]->gcount > 0) {
                      if (t->type == task_type_none) {
                        t->type = task_type_grav_mm;
                        t->ci = ci->progeny[j];
                        t->cj = cj->progeny[k];
                      } else
                        t = scheduler_addtask(
                            s, task_type_grav_mm, task_subtype_none, 0, 0,
                            ci->progeny[j], cj->progeny[k], 0);
                    }
                }
              redo = (t->type != task_type_none);

            }

            /* Otherwise, make a pp task out of it. */
            else
              t->type = task_type_grav_pp;
          }
        }

      } /* gravity pair interaction? */

    } /* gravity interaction? */

  } /* loop over all tasks. */
}

/**
 * @brief Add a #task to the #scheduler.
 *
 * @param s The #scheduler we are working in.
 * @param type The type of the task.
 * @param subtype The sub-type of the task.
 * @param flags The flags of the task.
 * @param wait
 * @param ci The first cell to interact.
 * @param cj The second cell to interact.
 * @param tight
 */

struct task *scheduler_addtask(struct scheduler *s, int type, int subtype,
                               int flags, int wait, struct cell *ci,
                               struct cell *cj, int tight) {

  int ind;
  struct task *t;

  /* Get the next free task. */
  ind = atomic_inc(&s->tasks_next);

  /* Overflow? */
  if (ind >= s->size) error("Task list overflow.");

  /* Get a pointer to the new task. */
  t = &s->tasks[ind];

  /* Copy the data. */
  t->type = type;
  t->subtype = subtype;
  t->flags = flags;
  t->wait = wait;
  t->ci = ci;
  t->cj = cj;
  t->skip = 0;
  t->tight = tight;
  t->implicit = 0;
  t->weight = 0;
  t->rank = 0;
  t->tic = 0;
  t->toc = 0;
  t->nr_unlock_tasks = 0;

  /* Init the lock. */
  lock_init(&t->lock);

  /* Add an index for it. */
  // lock_lock( &s->lock );
  s->tasks_ind[atomic_inc(&s->nr_tasks)] = ind;
  // lock_unlock_blind( &s->lock );

  /* Return a pointer to the new task. */
  return t;
}

/**
 * @brief Sort the tasks in topological order over all queues.
 *
 * @param s The #scheduler.
 */

void scheduler_ranktasks(struct scheduler *s) {

  int i, j = 0, k, temp, left = 0, rank;
  struct task *t, *tasks = s->tasks;
  int *tid = s->tasks_ind, nr_tasks = s->nr_tasks;

  /* Run throught the tasks and get all the waits right. */
  for (i = 0, k = 0; k < nr_tasks; k++) {
    tid[k] = k;
    for (j = 0; j < tasks[k].nr_unlock_tasks; j++)
      tasks[k].unlock_tasks[j]->wait += 1;
  }

  /* Main loop. */
  for (j = 0, rank = 0; left < nr_tasks; rank++) {

    /* Load the tids of tasks with no waits. */
    for (k = left; k < nr_tasks; k++)
      if (tasks[tid[k]].wait == 0) {
        temp = tid[j];
        tid[j] = tid[k];
        tid[k] = temp;
        j += 1;
      }

    /* Did we get anything? */
    if (j == left) error("Unsatisfiable task dependencies detected.");

    /* Unlock the next layer of tasks. */
    for (i = left; i < j; i++) {
      t = &tasks[tid[i]];
      t->rank = rank;
      tid[i] = t - tasks;
      if (tid[i] >= nr_tasks) error("Task index overshoot.");
      /* message( "task %i of type %s has rank %i." , i ,
          (t->type == task_type_self) ? "self" : (t->type == task_type_pair) ?
         "pair" : "sort" , rank ); */
      for (k = 0; k < t->nr_unlock_tasks; k++) t->unlock_tasks[k]->wait -= 1;
    }

    /* The new left (no, not tony). */
    left = j;
  }

  /* Verify that the tasks were ranked correctly. */
  /* for ( k = 1 ; k < s->nr_tasks ; k++ )
      if ( tasks[ tid[k-1] ].rank > tasks[ tid[k-1] ].rank )
          error( "Task ranking failed." ); */
}

/**
 * @brief (Re)allocate the task arrays.
 *
 * @param s The #scheduler.
 * @param size The maximum number of tasks in the #scheduler.
 */

void scheduler_reset(struct scheduler *s, int size) {

  int k;

  /* Do we need to re-allocate? */
  if (size > s->size) {

    /* Free exising task lists if necessary. */
    if (s->tasks != NULL) free(s->tasks);
    if (s->tasks_ind != NULL) free(s->tasks_ind);

    /* Allocate the new lists. */
    if ((s->tasks = (struct task *)malloc(sizeof(struct task) *size)) == NULL ||
        (s->tasks_ind = (int *)malloc(sizeof(int) * size)) == NULL)
      error("Failed to allocate task lists.");
  }

  /* Reset the task data. */
  bzero(s->tasks, sizeof(struct task) * size);

  /* Reset the counters. */
  s->size = size;
  s->nr_tasks = 0;
  s->tasks_next = 0;
  s->waiting = 0;

  /* Set the task pointers in the queues. */
  for (k = 0; k < s->nr_queues; k++) s->queues[k].tasks = s->tasks;
}

/**
 * @brief Compute the task weights
 *
 * @param s The #scheduler.
 */

void scheduler_reweight(struct scheduler *s) {

  int k, j, nr_tasks = s->nr_tasks, *tid = s->tasks_ind;
  struct task *t, *tasks = s->tasks;
  int nodeID = s->nodeID;
  float sid_scale[13] = {0.1897, 0.4025, 0.1897, 0.4025, 0.5788, 0.4025, 0.1897,
                         0.4025, 0.1897, 0.4025, 0.5788, 0.4025, 0.5788};
  float wscale = 0.001;
  // ticks tic;

  /* Run throught the tasks backwards and set their waits and
     weights. */
  // tic = getticks();
  for (k = nr_tasks - 1; k >= 0; k--) {
    t = &tasks[tid[k]];
    t->weight = 0;
    for (j = 0; j < t->nr_unlock_tasks; j++)
      if (t->unlock_tasks[j]->weight > t->weight)
        t->weight = t->unlock_tasks[j]->weight;
    if (!t->implicit && t->tic > 0)
      t->weight += wscale * (t->toc - t->tic);
    else
      switch (t->type) {
        case task_type_sort:
          t->weight += wscale * intrinsics_popcount(t->flags) * t->ci->count *
                       (sizeof(int) * 8 - intrinsics_clz(t->ci->count));
          break;
        case task_type_self:
          t->weight += 1 * t->ci->count * t->ci->count;
          break;
        case task_type_pair:
          if (t->ci->nodeID != nodeID || t->cj->nodeID != nodeID)
            t->weight +=
                3 * wscale * t->ci->count * t->cj->count * sid_scale[t->flags];
          else
            t->weight +=
                2 * wscale * t->ci->count * t->cj->count * sid_scale[t->flags];
          break;
        case task_type_sub:
          if (t->cj != NULL) {
            if (t->ci->nodeID != nodeID || t->cj->nodeID != nodeID) {
              if (t->flags < 0)
                t->weight += 3 * wscale * t->ci->count * t->cj->count;
              else
                t->weight += 3 * wscale * t->ci->count * t->cj->count *
                             sid_scale[t->flags];
            } else {
              if (t->flags < 0)
                t->weight += 2 * wscale * t->ci->count * t->cj->count;
              else
                t->weight += 2 * wscale * t->ci->count * t->cj->count *
                             sid_scale[t->flags];
            }
          } else
            t->weight += 1 * wscale * t->ci->count * t->ci->count;
          break;
        case task_type_ghost:
          if (t->ci == t->ci->super) t->weight += wscale * t->ci->count;
          break;
        case task_type_kick1:
        case task_type_kick2:
          t->weight += wscale * t->ci->count;
          break;
        default:
          break;
      }
    if (t->type == task_type_send) t->weight = INT_MAX / 8;
    if (t->type == task_type_recv) t->weight *= 1.41;
  }
  // message( "weighting tasks took %.3f ms." , (double)( getticks() - tic ) /
  // CPU_TPS * 1000 );

  /* int min = tasks[0].weight, max = tasks[0].weight;
  for ( k = 1 ; k < nr_tasks ; k++ )
      if ( tasks[k].weight < min )
          min = tasks[k].weight;
      else if ( tasks[k].weight > max )
          max = tasks[k].weight;
  message( "task weights are in [ %i , %i ]." , min , max ); */
}

/**
 * @brief Start the scheduler, i.e. fill the queues with ready tasks.
 *
 * @param s The #scheduler.
 * @param mask The task types to enqueue.
 */

void scheduler_start(struct scheduler *s, unsigned int mask) {

  int k, j, nr_tasks = s->nr_tasks, *tid = s->tasks_ind;
  struct task *t, *tasks = s->tasks;
  // ticks tic;

  /* Run throught the tasks and set their waits. */
  // tic = getticks();
  for (k = nr_tasks - 1; k >= 0; k--) {
    t = &tasks[tid[k]];
    t->wait = 0;
    t->rid = -1;
    if (!((1 << t->type) & mask) || t->skip) continue;
    for (j = 0; j < t->nr_unlock_tasks; j++)
      atomic_inc(&t->unlock_tasks[j]->wait);
  }
  // message( "waiting tasks took %.3f ms." , (double)( getticks() - tic ) /
  // CPU_TPS * 1000 );

  /* Don't enqueue link tasks directly. */
  mask &= ~(1 << task_type_link);

  /* Loop over the tasks and enqueue whoever is ready. */
  // tic = getticks();
  for (k = 0; k < nr_tasks; k++) {
    t = &tasks[tid[k]];
    if (((1 << t->type) & mask) && !t->skip) {
      if (t->wait == 0) {
        scheduler_enqueue(s, t);
        pthread_cond_broadcast(&s->sleep_cond);
      } else
        break;
    }
  }
  // message( "enqueueing tasks took %.3f ms." , (double)( getticks() - tic ) /
  // CPU_TPS * 1000 );
}

/**
 * @brief Put a task on one of the queues.
 *
 * @param s The #scheduler.
 * @param t The #task.
 */

void scheduler_enqueue(struct scheduler *s, struct task *t) {

  int qid = -1;
#ifdef WITH_MPI
  int err;
#endif

  /* Ignore skipped tasks. */
  if (t->skip || atomic_cas(&t->rid, -1, 0) != -1) return;

  /* If this is an implicit task, just pretend it's done. */
  if (t->implicit) {
    for (int j = 0; j < t->nr_unlock_tasks; j++) {
      struct task *t2 = t->unlock_tasks[j];
      if (atomic_dec(&t2->wait) == 1 && !t2->skip) scheduler_enqueue(s, t2);
    }
  }

  /* Otherwise, look for a suitable queue. */
  else {

    /* Find the previous owner for each task type, and do
       any pre-processing needed. */
    switch (t->type) {
      case task_type_self:
      case task_type_sort:
      case task_type_ghost:
      case task_type_kick2:
        qid = t->ci->super->owner;
        break;
      case task_type_pair:
      case task_type_sub:
        qid = t->ci->super->owner;
        if (t->cj != NULL &&
            (qid < 0 ||
             s->queues[qid].count > s->queues[t->cj->super->owner].count))
          qid = t->cj->super->owner;
        break;
      case task_type_recv:
#ifdef WITH_MPI
        if ((err = MPI_Irecv(t->ci->parts, sizeof(struct part) * t->ci->count,
                             MPI_BYTE, t->ci->nodeID, t->flags, MPI_COMM_WORLD,
                             &t->req)) != MPI_SUCCESS) {
          char buff[MPI_MAX_ERROR_STRING];
          int len;
          MPI_Error_string(err, buff, &len);
          error("Failed to emit irecv for particle data (%s).", buff);
        }
        //message("recieving %i parts with tag=%i from %i to %i.",
        //        t->ci->count, t->flags, t->ci->nodeID, s->nodeID);
        //fflush(stdout);
        qid = 1 % s->nr_queues;
#else
        error("SWIFT was not compiled with MPI support.");
#endif
        break;
      case task_type_send:
#ifdef WITH_MPI
        for ( int k = 0 ; k < t->ci->count ; k++ ) {
          t->ci->parts[k].lastNodeID = s->nodeID + 10000;
        }

        if ((err = MPI_Isend(t->ci->parts, sizeof(struct part) * t->ci->count,
                             MPI_BYTE, t->cj->nodeID, t->flags, MPI_COMM_WORLD,
                             &t->req)) != MPI_SUCCESS) {
          char buff[MPI_MAX_ERROR_STRING];
          int len;
          MPI_Error_string(err, buff, &len);
          error("Failed to emit isend for particle data (%s).", buff);
        }
        //message( "sending %i parts with tag=%i from %i to %i.",
        //         t->ci->count, t->flags, s->nodeID, t->cj->nodeID );
        //fflush(stdout);
        qid = 0;
#else
        error("SWIFT was not compiled with MPI support.");
#endif
        break;
      default:
        qid = -1;
    }

    if (qid >= s->nr_queues) error("Bad computed qid.");

    /* If no previous owner, find the shortest queue. */
    if (qid < 0) qid = rand() % s->nr_queues;

    /* Increase the waiting counter. */
    atomic_inc(&s->waiting);

    /* Insert the task into that queue. */
    queue_insert(&s->queues[qid], t);
  }
}

/**
 * @brief Take care of a tasks dependencies.
 *
 * @param s The #scheduler.
 * @param t The finished #task.
 *
 * @return A pointer to the next task, if a suitable one has
 *         been identified.
 */

struct task *scheduler_done(struct scheduler *s, struct task *t) {

  int k, res;
  struct task *t2, *next = NULL;
  struct cell *super = t->ci->super;

  /* Release whatever locks this task held. */
  if (!t->implicit) task_unlock(t);

  /* Loop through the dependencies and add them to a queue if
     they are ready. */
  for (k = 0; k < t->nr_unlock_tasks; k++) {
    t2 = t->unlock_tasks[k];
    if ((res = atomic_dec(&t2->wait)) < 1) error("Negative wait!");
    if (res == 1 && !t2->skip) {
      if (0 && !t2->implicit && t2->ci->super == super &&
          (next == NULL || t2->weight > next->weight) && task_lock(t2)) {
        if (next != NULL) {
          task_unlock(next);
          scheduler_enqueue(s, next);
        }
        next = t2;
      } else
        scheduler_enqueue(s, t2);
    }
  }

  /* Task definitely done. */
  if (!t->implicit) {
    t->toc = getticks();
    pthread_mutex_lock(&s->sleep_mutex);
    if (next == NULL) atomic_dec(&s->waiting);
    pthread_cond_broadcast(&s->sleep_cond);
    pthread_mutex_unlock(&s->sleep_mutex);
  }

  /* Start the clock on the follow-up task. */
  if (next != NULL) next->tic = getticks();

  /* Return the next best task. */
  return next;
}

/**
 * @brief Resolve a single dependency by hand.
 *
 * @param s The #scheduler.
 * @param t The dependent #task.
 *
 * @return A pointer to the next task, if a suitable one has
 *         been identified.
 */

struct task *scheduler_unlock(struct scheduler *s, struct task *t) {

  int k, res;
  struct task *t2, *next = NULL;

  /* Loop through the dependencies and add them to a queue if
     they are ready. */
  for (k = 0; k < t->nr_unlock_tasks; k++) {
    t2 = t->unlock_tasks[k];
    if ((res = atomic_dec(&t2->wait)) < 1) error("Negative wait!");
    if (res == 1 && !t2->skip) scheduler_enqueue(s, t2);
  }

  /* Task definitely done. */
  if (!t->implicit) {
    t->toc = getticks();
    pthread_mutex_lock(&s->sleep_mutex);
    if (next == NULL) atomic_dec(&s->waiting);
    pthread_cond_broadcast(&s->sleep_cond);
    pthread_mutex_unlock(&s->sleep_mutex);
  }

  /* Start the clock on the follow-up task. */
  if (next != NULL) next->tic = getticks();

  /* Return the next best task. */
  return next;
}

/**
 * @brief Get a task, preferably from the given queue.
 *
 * @param s The #scheduler.
 * @param qid The ID of the prefered #queue.
 * @param super the super-cell
 *
 * @return A pointer to a #task or @c NULL if there are no available tasks.
 */

struct task *scheduler_gettask(struct scheduler *s, int qid,
                               struct cell *super) {

  struct task *res = NULL;
  int k, nr_queues = s->nr_queues;
  unsigned int seed = qid;

  /* Check qid. */
  if (qid >= nr_queues || qid < 0) error("Bad queue ID.");

  /* Loop as long as there are tasks... */
  while (s->waiting > 0 && res == NULL) {

    /* Try more than once before sleeping. */
    for (int tries = 0; res == NULL && s->waiting && tries < scheduler_maxtries;
         tries++) {

      /* Try to get a task from the suggested queue. */
      if (s->queues[qid].count > 0) {
        TIMER_TIC
        res = queue_gettask(&s->queues[qid], super, 0);
        TIMER_TOC(timer_qget);
        if (res != NULL) break;
      }

      /* If unsucessful, try stealing from the other queues. */
      if (s->flags & scheduler_flag_steal) {
        int count = 0, qids[nr_queues];
        for (k = 0; k < nr_queues; k++)
          if (s->queues[k].count > 0) qids[count++] = k;
        for (k = 0; k < scheduler_maxsteal && count > 0; k++) {
          int ind = rand_r(&seed) % count;
          TIMER_TIC
          res = queue_gettask(&s->queues[qids[ind]], super, 0);
          TIMER_TOC(timer_qsteal);
          if (res != NULL)
            break;
          else
            qids[ind] = qids[--count];
        }
        if (res != NULL) break;
      }
    }

/* If we failed, take a short nap. */
#ifdef WITH_MPI
    if (res == NULL && qid > 1) {
#else
    if (res == NULL) {
#endif
      pthread_mutex_lock(&s->sleep_mutex);
      if (s->waiting > 0) pthread_cond_wait(&s->sleep_cond, &s->sleep_mutex);
      pthread_mutex_unlock(&s->sleep_mutex);
    }
  }

  /* Start the timer on this task, if we got one. */
  if (res != NULL) {
    res->tic = getticks();
    res->rid = qid;
  }

  /* No milk today. */
  return res;
}

/**
 * @brief Initialize the #scheduler.
 *
 * @param s The #scheduler.
 * @param space The #space we are working with
 * @param nr_queues The number of queues in this scheduler.
 * @param flags The #scheduler flags.
 * @param nodeID The MPI rank
 */

void scheduler_init(struct scheduler *s, struct space *space, int nr_queues,
                    unsigned int flags, int nodeID) {

  int k;

  /* Init the lock. */
  lock_init(&s->lock);

  /* Allocate the queues. */
  if ((s->queues = (struct queue *)malloc(sizeof(struct queue) * nr_queues)) ==
      NULL)
    error("Failed to allocate queues.");

  /* Initialize each queue. */
  for (k = 0; k < nr_queues; k++) queue_init(&s->queues[k], NULL);

  /* Init the sleep mutex and cond. */
  if (pthread_cond_init(&s->sleep_cond, NULL) != 0 ||
      pthread_mutex_init(&s->sleep_mutex, NULL) != 0)
    error("Failed to initialize sleep barrier.");

  /* Set the scheduler variables. */
  s->nr_queues = nr_queues;
  s->flags = flags;
  s->space = space;
  s->nodeID = nodeID;

  /* Init other values. */
  s->tasks = NULL;
  s->tasks_ind = NULL;
  s->waiting = 0;
  s->size = 0;
  s->nr_tasks = 0;
  s->tasks_next = 0;
}
