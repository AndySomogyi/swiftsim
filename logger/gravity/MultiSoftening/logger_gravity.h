/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2020 Loic Hausammann (loic.hausammann@epfl.ch)
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
#ifndef SWIFT_MULTISOFTENING_LOGGER_GRAVITY_H
#define SWIFT_MULTISOFTENING_LOGGER_GRAVITY_H

#include "../config.h"
/**
   TODO

   Merge everything into writer
   Use NaN to flag available fields
*/

/* local includes */
#include "logger_loader_io.h"
#include "logger_python_tools.h"
#include "gravity_io.h"

/* Index of the mask in the header mask array */
extern int gravity_logger_mask_id[gravity_logger_field_count];

/**
 * @brief When starting to read a logfile, check the required fields in the
 * logfile's header.
 *
 * @param head The #header.
 */
__attribute__((always_inline)) INLINE static void gravity_logger_reader_populate_mask_data(
    struct header *head) {

  for (int i = 0; i < head->masks_count; i++) {
    int size = 0;
    if (strcmp(head->masks[i].name, gravity_logger_field_names[gravity_logger_field_coordinates]) == 0) {
      size = 3 * sizeof(double);
      gravity_logger_mask_id[gravity_logger_field_coordinates] = i;
    } else if (strcmp(head->masks[i].name, gravity_logger_field_names[gravity_logger_field_velocities]) ==
               0) {
      size = 3 * sizeof(float);
      gravity_logger_mask_id[gravity_logger_field_velocities] = i;

    } else if (strcmp(head->masks[i].name, gravity_logger_field_names[gravity_logger_field_accelerations]) ==
               0) {
      size = 3 * sizeof(float);
      gravity_logger_mask_id[gravity_logger_field_accelerations] = i;

    } else if (strcmp(head->masks[i].name, gravity_logger_field_names[gravity_logger_field_masses]) == 0) {
      size = sizeof(float);
      gravity_logger_mask_id[gravity_logger_field_masses] = i;

    } else if (strcmp(head->masks[i].name, gravity_logger_field_names[gravity_logger_field_particle_ids]) == 0) {
      size = sizeof(uint64_t);
      gravity_logger_mask_id[gravity_logger_field_particle_ids] = i;
    }

    /* Check that the size are compatible */
    if (size != 0 && size != head->masks[i].size) {
      error("Size are not compatible for the field %s", head->masks[i].name);
    }

  }
}

/**
 * @brief Interpolate a field of the particle at the given time.
 * Here we use a linear interpolation for most of the fields.
 * For the position (velocity), we use a quintic (cubic) hermite interpolation
 * based on the positions, velocities and accelerations at the time of the two
 * particles.
 *
 * @param field_before Pointer to the field at a time < t.
 * @param field_after Pointer to the field at a time > t.
 * @param otuput Pointer to the output value.
 * @param t_before Time of field_before (< t).
 * @param t_after Time of field_after (> t).
 * @param t Requested time.
 * @param field The field to reconstruct (follows the order of #gravity_logger_fields).
 */
__attribute__((always_inline)) INLINE static void
gravity_logger_interpolate_field(
    void *field_before, void *field_after,
    void *output, const double t_before, const double t_after,
    const double t, const int field) {

#ifdef SWIFT_DEBUG_CHECKS
  /* Check the times */
  if (t_before > t || t_after < t) {
    error("The times for the interpolation are not correct"
          " %g < %g < %g.", t_before, t, t_after);
  }
#endif

  /* Compute the interpolation scaling. */
  const double wa =
      (t - t_before) / (t_after - t_before);
  const double wb = 1. - wa;

  switch(field) {
    case gravity_logger_field_coordinates:
      /* interpolate vectors. */
      // TODO use hermite spline
      for (int i = 0; i < 3; i++) {
        ((double *)output)[i] = wa * ((double *) field_after)[i] +
          wb * ((double *) field_before)[i];
        /* /\* position *\/ */
        /* ((double *)output)[i] = logger_tools_quintic_hermite_spline( */
        /*     t_before, before->x[i], before->v_full[i], before->a_grav[i], */
        /*     t_after, after->x[i], after->v_full[i], after->a_grav[i], */
        /*     t); */
      }
      break;
    case gravity_logger_field_velocities:
      /* interpolate vectors. */
      // TODO use hermite spline
      for (int i = 0; i < 3; i++) {
        ((float *)output)[i] = wa * ((float *) field_after)[i] +
          wb * ((float *) field_before)[i];
      }
      break;
    case gravity_logger_field_accelerations:
      /* interpolate vectors. */
      for (int i = 0; i < 3; i++) {
        ((float *)output)[i] = wa * ((float *) field_after)[i] +
          wb * ((float *) field_before)[i];
      }
      break;
    case gravity_logger_field_masses:
      ((float *)output)[0] = wa * ((float *) field_after)[0] +
        wb * ((float *) field_before)[0];
      break;
    case gravity_logger_field_particle_ids:
      if (*(long long *) field_after != *(long long *) field_before) {
        error("Interpolating different particles");
      }
      *(long long *) output = *(long long *) field_after;
      break;
    default:
      error("Not implemented");
  }
}

#ifdef HAVE_PYTHON
__attribute__((always_inline)) INLINE static void
gravity_logger_generate_python(struct logger_python_field *fields) {

  fields[gravity_logger_field_coordinates] = logger_loader_python_field(/* Dimension */ 3, NPY_DOUBLE);
  fields[gravity_logger_field_velocities] = logger_loader_python_field(/* Dimension */ 3, NPY_FLOAT32);
  fields[gravity_logger_field_accelerations] = logger_loader_python_field(/* Dimension */ 3, NPY_FLOAT32);
  fields[gravity_logger_field_masses] = logger_loader_python_field(/* Dimension */ 1, NPY_FLOAT32);
  fields[gravity_logger_field_particle_ids] = logger_loader_python_field(/* Dimension */ 1, NPY_LONGLONG);
}


#endif  // HAVE_PYTHON
#endif  // SWIFT_MULTISOFTENING_LOGGER_GRAVITY_H