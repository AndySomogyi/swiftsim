/*******************************************************************************
 * This file is part of SWIFT.
 * Coypright (c) 2017 Matthieu Schaller (matthieu.schaller@durham.ac.uk)
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
#ifndef SWIFT_DEBUG_INTERACTIONS_HYDRO_H
#define SWIFT_DEBUG_INTERACTIONS_HYDRO_H

/**
 * @file DebugInteractions/hydro.h
 * @brief Empty SPH implementation used solely to test the SELF/PAIR routines.
 */

#include "equation_of_state.h"
#include "hydro_properties.h"
#include "hydro_space.h"
#include "part.h"

#include <float.h>

/**
 * @brief Returns the internal energy of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_internal_energy(
    const struct part *restrict p) {

  return 0.f;
}

/**
 * @brief Returns the pressure of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_pressure(
    const struct part *restrict p) {

  return 0.f;
}

/**
 * @brief Returns the entropy of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_entropy(
    const struct part *restrict p) {

  return 0.f;
}

/**
 * @brief Returns the sound speed of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_soundspeed(
    const struct part *restrict p) {

  return 0.f;
}

/**
 * @brief Returns the density of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_density(
    const struct part *restrict p) {

  return 0.f;
}

/**
 * @brief Returns the mass of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_mass(
    const struct part *restrict p) {
  return 0.f;
}

/**
 * @brief Returns the time derivative of internal energy of a particle
 *
 * We assume a constant density.
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_internal_energy_dt(
    const struct part *restrict p) {
  return 0.f;
}

/**
 * @brief Returns the time derivative of internal energy of a particle
 *
 * We assume a constant density.
 *
 * @param p The particle of interest.
 * @param du_dt The new time derivative of the internal energy.
 */
__attribute__((always_inline)) INLINE static void hydro_set_internal_energy_dt(
    struct part *restrict p, float du_dt) {}

/**
 * @brief Computes the hydro time-step of a given particle
 *
 * @param p Pointer to the particle data
 * @param xp Pointer to the extended particle data
 *
 */
__attribute__((always_inline)) INLINE static float hydro_compute_timestep(
    const struct part *restrict p, const struct xpart *restrict xp,
    const struct hydro_props *restrict hydro_properties) {

  return FLT_MAX;
}

/**
 * @brief Does some extra hydro operations once the actual physical time step
 * for the particle is known.
 *
 * @param p The particle to act upon.
 * @param dt Physical time step of the particle during the next step.
 */
__attribute__((always_inline)) INLINE static void hydro_timestep_extra(
    struct part *p, float dt) {}

/**
 * @brief Prepares a particle for the density calculation.
 *
 * Zeroes all the relevant arrays in preparation for the sums taking place in
 * the variaous density tasks
 *
 * @param p The particle to act upon
 * @param hs #hydro_space containing hydro specific space information.
 */
__attribute__((always_inline)) INLINE static void hydro_init_part(
    struct part *restrict p, const struct hydro_space *hs) {

  for (int i = 0; i < 256; ++i) p->ids_ngbs_density[i] = -1;
  p->num_ngb_density = 0;

  p->density.wcount = 0.f;
  p->density.wcount_dh = 0.f;
}

/**
 * @brief Finishes the density calculation.
 *
 * Multiplies the density and number of neighbours by the appropiate constants
 * and add the self-contribution term.
 *
 * @param p The particle to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_end_density(
    struct part *restrict p) {

  /* Some smoothing length multiples. */
  const float h = p->h;
  const float h_inv = 1.0f / h;                       /* 1/h */
  const float h_inv_dim = pow_dimension(h_inv);       /* 1/h^d */
  const float h_inv_dim_plus_one = h_inv_dim * h_inv; /* 1/h^(d+1) */

  /* Final operation on the density (add self-contribution). */
  p->density.wcount += kernel_root;
  p->density.wcount_dh -= hydro_dimension * kernel_root;

  /* Finish the calculation by inserting the missing h-factors */
  p->density.wcount *= h_inv_dim;
  p->density.wcount_dh *= h_inv_dim_plus_one;
}

/**
 * @brief Sets all particle fields to sensible values when the #part has 0 ngbs.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_part_has_no_neighbours(
    struct part *restrict p, struct xpart *restrict xp) {

  /* Some smoothing length multiples. */
  const float h = p->h;
  const float h_inv = 1.0f / h;                 /* 1/h */
  const float h_inv_dim = pow_dimension(h_inv); /* 1/h^d */

  /* Re-set problematic values */
  p->density.wcount = kernel_root * kernel_norm * h_inv_dim;
  p->density.wcount_dh = 0.f;
}

/**
 * @brief Prepare a particle for the force calculation.
 *
 * Computes viscosity term, conduction term and smoothing length gradient terms.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 * @param ti_current The current time (on the timeline)
 * @param timeBase The minimal time-step size
 */
__attribute__((always_inline)) INLINE static void hydro_prepare_force(
    struct part *restrict p, struct xpart *restrict xp) {}

/**
 * @brief Reset acceleration fields of a particle
 *
 * Resets all hydro acceleration and time derivative fields in preparation
 * for the sums taking place in the variaous force tasks
 *
 * @param p The particle to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_reset_acceleration(
    struct part *restrict p) {

  for (int i = 0; i < 256; ++i) p->ids_ngbs_force[i] = -1;
  p->num_ngb_force = 0;

  p->force.h_dt = 0.f;
}

/**
 * @brief Sets the values to be predicted in the drifts to their values at a
 * kick time
 *
 * @param p The particle.
 * @param xp The extended data of this particle.
 */
__attribute__((always_inline)) INLINE static void hydro_reset_predicted_values(
    struct part *restrict p, const struct xpart *restrict xp) {}

/**
 * @brief Predict additional particle fields forward in time when drifting
 *
 * @param p The particle
 * @param xp The extended data of the particle
 * @param dt The drift time-step.
 * @param t0 The time at the start of the drift
 * @param t1 The time at the end of the drift
 * @param timeBase The minimal time-step size
 */
__attribute__((always_inline)) INLINE static void hydro_predict_extra(
    struct part *restrict p, const struct xpart *restrict xp, float dt) {}

/**
 * @brief Finishes the force calculation.
 *
 * Multiplies the forces and accelerationsby the appropiate constants
 *
 * @param p The particle to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_end_force(
    struct part *restrict p) {}

/**
 * @brief Kick the additional variables
 *
 * @param p The particle to act upon
 * @param xp The particle extended data to act upon
 * @param dt The time-step for this kick
 */
__attribute__((always_inline)) INLINE static void hydro_kick_extra(
    struct part *restrict p, struct xpart *restrict xp, float dt) {}

/**
 *  @brief Converts hydro quantity of a particle at the start of a run
 *
 * Requires the density to be known
 *
 * @param p The particle to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_convert_quantities(
    struct part *restrict p, struct xpart *restrict xp) {}

/**
 * @brief Initialises the particles for the first time
 *
 * This function is called only once just after the ICs have been
 * read in to do some conversions.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_first_init_part(
    struct part *restrict p, struct xpart *restrict xp) {

  p->time_bin = 0;
  xp->v_full[0] = p->v[0];
  xp->v_full[1] = p->v[1];
  xp->v_full[2] = p->v[2];

  hydro_reset_acceleration(p);
  hydro_init_part(p, NULL);
}

#endif /* SWIFT_DEBUG_INTERACTIONS_HYDRO_H */