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

/* Include corresponding header */
#include "logger_particle_array.h"

/**
 * @brief Initialize the #logger_particle_array structure.
 *
 * @param array The array to initialize.
 */
void logger_particle_array_init(struct logger_particle_array *array) {
  /* Hydro */
  array->hydro.n = 0;
  array->hydro.parts = NULL;

  /* Dark matter */
  array->grav.n = 0;
  array->grav.parts = NULL;

  /* Stars */
  array->stars.n = 0;
  array->stars.parts = NULL;
}

/**
 * @brief The allocate the required memory.
 *
 * @param array The array to allocate.
 * @param n_part The number of #logger_particle.
 * @param n_gpart The number of #logger_gparticle.
 * @param n_spart The number of #logger_sparticle.
 */
void logger_particle_array_allocate(struct logger_particle_array *array,
                                    size_t n_part, size_t n_gpart,
                                    size_t n_spart) {
  /* Hydro */
  array->hydro.n = n_part;
  array->hydro.parts =
      (struct logger_particle *)malloc(n_part * sizeof(struct logger_particle));
  if (array->hydro.parts == NULL) {
    error("Failed to allocate the hydro particles");
  }

  /* Gravity */
  array->grav.n = n_part;
  array->grav.parts = (struct logger_gparticle *)malloc(
      n_gpart * sizeof(struct logger_gparticle));
  if (array->grav.parts == NULL) {
    error("Failed to allocate the gravity particles");
  }

  /* Stars */
  array->stars.n = n_part;
  array->stars.parts = (struct logger_sparticle *)malloc(
      n_spart * sizeof(struct logger_sparticle));
  if (array->stars.parts == NULL) {
    error("Failed to allocate the stars particles");
  }
}

/**
 * @brief Free the allocated memory.
 *
 * @param array The array to free.
 */
void logger_particle_array_free(struct logger_particle_array *array) {
  free(array->hydro.parts);
  free(array->grav.parts);
  free(array->stars.parts);

  logger_particle_array_init(array);
}

/**
 * @brief Change the size of the allocated memory.
 *
 * @param array The array to increase.
 * @param new_n_part The new number of (hydro) particles.
 * @param new_n_gpart The new number of (gravity) particles.
 * @param new_n_spart The new number of (stars) particles.
 */
void logger_particle_array_change_size(struct logger_particle_array *array,
                                       size_t new_n_part, size_t new_n_gpart,
                                       size_t new_n_spart) {
  /* Hydro */
  /* Check if need to free memory */
  if (new_n_part == 0 && array->hydro.n != 0) {
    free(array->hydro.parts);
    array->hydro.parts = NULL;
  }
  /* Check if need to change the size */
  else if (new_n_part != array->hydro.n) {
    /* Allocate the new array */
    struct logger_particle *parts = (struct logger_particle *)malloc(
        new_n_part * sizeof(struct logger_particle));
    if (parts == NULL) {
      error("Failed to allocate the hydro particles.");
    }

    /* Copy the previous particles */
    if (array->hydro.n != 0) {
      memcpy(parts, array->hydro.parts,
             array->hydro.n * sizeof(struct logger_particle));
    }
    free(array->hydro.parts);
    array->hydro.parts = parts;
  }

  array->hydro.n = new_n_part;

  /* Dark matter */
  /* Check if need to free memory */
  if (new_n_gpart == 0 && array->grav.n != 0) {
    free(array->grav.parts);
    array->grav.parts = NULL;
  }
  /* Check if need to change the size */
  else if (new_n_gpart != array->grav.n) {
    /* Allocate the new array */
    struct logger_gparticle *parts = (struct logger_gparticle *)malloc(
        new_n_gpart * sizeof(struct logger_gparticle));
    if (parts == NULL) {
      error("Failed to allocate the dark matter particles.");
    }

    /* Copy the previous particles */
    if (array->grav.n != 0) {
      memcpy(parts, array->grav.parts,
             array->grav.n * sizeof(struct logger_gparticle));
    }
    free(array->grav.parts);
    array->grav.parts = parts;
  }

  array->grav.n = new_n_gpart;

  /* Stars */
  /* Check if need to free memory */
  if (new_n_spart == 0 && array->stars.n != 0) {
    free(array->stars.parts);
    array->stars.parts = NULL;
  }
  /* Check if need to change the size */
  else if (new_n_spart != array->stars.n) {
    /* Allocate the new array */
    struct logger_sparticle *parts = (struct logger_sparticle *)malloc(
        new_n_spart * sizeof(struct logger_sparticle));
    if (parts == NULL) {
      error("Failed to allocate the stars particles.");
    }

    /* Copy the previous particles */
    if (array->stars.n != 0) {
      memcpy(parts, array->stars.parts,
             array->stars.n * sizeof(struct logger_sparticle));
    }
    free(array->stars.parts);
    array->stars.parts = parts;
  }

  array->stars.n = new_n_spart;
}

/**
 * @brief Initialize a dynamic array with the default initial size.
 *
 * @param array The #logger_dynamic_particle_array.
 * @param default_size The initial size of all the particle arrays.
 */
void logger_dynamic_particle_array_init(
    struct logger_dynamic_particle_array *array, int default_size) {
  /* We do not have any particles yet */
  array->n_grav = 0;
  array->n_hydro = 0;
  array->n_stars = 0;

  /* Allocate the array */
  logger_particle_array_init(&array->array);
  logger_particle_array_allocate(&array->array, default_size, default_size, default_size);
}

/**
 * @brief Add an hydro particle (save only the offset).
 *
 * @param array The #logger_dynamic_particle_array.
 * @param offset The offset of the new particle in the logfile.
 */
void logger_dynamic_particle_array_add_hydro(
    struct logger_dynamic_particle_array *array, size_t offset) {
  /* Save the offset */
  array->array.hydro.parts[array->n_hydro].offset = offset;

  /* Update the number of particles */
  array->n_hydro++;

  /* Increase the size if required */
  if (array->n_hydro == array->array.hydro.n) {
    logger_particle_array_change_size(
      &array->array, 2 * array->array.hydro.n,
      array->array.grav.n, array->array.stars.n);
  }
}

/**
 * @brief Add a star (save only the offset).
 *
 * @param array The #logger_dynamic_particle_array.
 * @param offset The offset of the new particle in the logfile.
 */
void logger_dynamic_particle_array_add_stars(
    struct logger_dynamic_particle_array *array, size_t offset) {
  /* Save the offset */
  array->array.stars.parts[array->n_stars].offset = offset;

  /* Update the number of particles */
  array->n_stars++;

  /* Increase the size if required */
  if (array->n_stars == array->array.stars.n) {
    logger_particle_array_change_size(
      &array->array, array->array.hydro.n,
      array->array.grav.n, 2 * array->array.stars.n);
  }

}

/**
 * @brief Add a gravity particle (save only the offset).
 *
 * @param array The #logger_dynamic_particle_array.
 * @param offset The offset of the new particle in the logfile.
 */
void logger_dynamic_particle_array_add_gravity(
    struct logger_dynamic_particle_array *array, size_t offset) {

  /* Save the offset */
  array->array.grav.parts[array->n_grav].offset = offset;

  /* Update the number of particles */
  array->n_grav++;

  /* Increase the size if required */
  if (array->n_grav == array->array.grav.n) {
    logger_particle_array_change_size(
      &array->array, 2 * array->array.hydro.n,
      array->array.grav.n, array->array.stars.n);
  }

}

/**
 * @brief Free the allocated memory.
 *
 * @param array The #logger_dynamic_particle_array.
 */
void logger_dynamic_particle_array_free(
    struct logger_dynamic_particle_array *array) {

  logger_particle_array_free(&array->array);
  /* Reset the counters */
  array->n_grav = 0;
  array->n_stars = 0;
  array->n_hydro = 0;
}
