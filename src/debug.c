/*******************************************************************************
 * This file is part of SWIFT.
 * Coypright (c) 2013 Matthieu Schaller (matthieu.schaller@durham.ac.uk),
 *                    Pedro Gonnet (pedro.gonnet@durham.ac.uk).
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

#include <stdio.h>

/* This object's header. */
#include "debug.h"

/**
 * @brief Dump the information pertaining to the given cell.
 */

void print_cell(struct cell *c) {
  printf(
      "## Cell 0x%0zx: loc=[%.3e,%.3e,%.3e], h=[%.3e,%.3e,%.3e], depth=%i, "
      "split=%i, maxdepth=%i.\n",
      (size_t)c, c->loc[0], c->loc[1], c->loc[2], c->h[0], c->h[1], c->h[2],
      c->depth, c->split, c->maxdepth);
}

/**
 * @brief Looks for the particle with the given id and prints its information to
 *the standard output.
 *
 * @param parts The array of particles.
 * @param id The id too look for.
 * @param N The size of the array of particles.
 *
 * (Should be used for debugging only as it runs in O(N).)
 */

void printParticle(struct part *parts, long long int id, int N) {

  int i, found = 0;

  /* Look for the particle. */
  for (i = 0; i < N; i++)
    if (parts[i].id == id) {
      printf(
          "## Particle[%d]: id=%lld, x=[%.16e,%.16e,%.16e], "
          "v=[%.3e,%.3e,%.3e], a=[%.3e,%.3e,%.3e], h=%.3e, h_dt=%.3e, "
          "wcount=%.3e, m=%.3e, rho=%.3e, rho_dh=%.3e, div_v=%.3e, u=%.3e, "
          "dudt=%.3e, bals=%.3e, POrho2=%.3e, v_sig=%.3e, dt=%.3e\n",
          i, parts[i].id, parts[i].x[0], parts[i].x[1], parts[i].x[2],
          parts[i].v[0], parts[i].v[1], parts[i].v[2], parts[i].a[0],
          parts[i].a[1], parts[i].a[2], parts[i].h, parts[i].force.h_dt,
          parts[i].density.wcount, parts[i].mass, parts[i].rho, parts[i].rho_dh,
          parts[i].density.div_v, parts[i].u, parts[i].force.u_dt,
          parts[i].force.balsara, parts[i].force.POrho2, parts[i].force.v_sig,
          parts[i].dt);
      found = 1;
    }

  if (!found) printf("## Particles[???] id=%lld not found\n", id);
}

void printgParticle(struct gpart *parts, long long int id, int N) {

  int i, found = 0;

  /* Look for the particle. */
  for (i = 0; i < N; i++)
    if (parts[i].id == -id || (parts[i].id > 0 && parts[i].part->id == id)) {
      printf(
          "## gParticle[%d]: id=%lld, x=[%.16e,%.16e,%.16e], "
          "v=[%.3e,%.3e,%.3e], a=[%.3e,%.3e,%.3e], m=%.3e, dt=%.3e\n",
          i, (parts[i].id < 0) ? -parts[i].id : parts[i].part->id,
          parts[i].x[0], parts[i].x[1], parts[i].x[2], parts[i].v[0],
          parts[i].v[1], parts[i].v[2], parts[i].a[0], parts[i].a[1],
          parts[i].a[2], parts[i].mass, parts[i].dt);
      found = 1;
    }

  if (!found) printf("## Particles[???] id=%lld not found\n", id);
}

/**
 * @brief Prints the details of a given particle to stdout
 *
 * @param p The particle to print
 *
 */

void printParticle_single(struct part *p) {

  printf(
      "## Particle: id=%lld, x=[%e,%e,%e], v=[%.3e,%.3e,%.3e], "
      "a=[%.3e,%.3e,%.3e], h=%.3e, h_dt=%.3e, wcount=%.3e, m=%.3e, rho=%.3e, "
      "rho_dh=%.3e, div_v=%.3e, u=%.3e, dudt=%.3e, bals=%.3e, POrho2=%.3e, "
      "v_sig=%.3e, dt=%.3e\n",
      p->id, p->x[0], p->x[1], p->x[2], p->v[0], p->v[1], p->v[2], p->a[0],
      p->a[1], p->a[2], p->h, p->force.h_dt, p->density.wcount, p->mass, p->rho,
      p->rho_dh, p->density.div_v, p->u, p->force.u_dt, p->force.balsara,
      p->force.POrho2, p->force.v_sig, p->dt);
}
