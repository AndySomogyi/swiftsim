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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <omp.h>

/* Local headers. */
#include "cycle.h"
#include "atomic.h"
#include "lock.h"
#include "task.h"
#include "kernel.h"
#include "part.h"
#include "cell.h"
#include "space.h"
#include "runner.h"
#include "error.h"

/* Split size. */
int space_splitsize = space_splitsize_default;
int space_subsize = space_subsize_default;

/* Map shift vector to sortlist. */
const int sortlistID[27] = {
    /* ( -1 , -1 , -1 ) */   0 ,
    /* ( -1 , -1 ,  0 ) */   1 , 
    /* ( -1 , -1 ,  1 ) */   2 ,
    /* ( -1 ,  0 , -1 ) */   3 ,
    /* ( -1 ,  0 ,  0 ) */   4 , 
    /* ( -1 ,  0 ,  1 ) */   5 ,
    /* ( -1 ,  1 , -1 ) */   6 ,
    /* ( -1 ,  1 ,  0 ) */   7 , 
    /* ( -1 ,  1 ,  1 ) */   8 ,
    /* (  0 , -1 , -1 ) */   9 ,
    /* (  0 , -1 ,  0 ) */   10 , 
    /* (  0 , -1 ,  1 ) */   11 ,
    /* (  0 ,  0 , -1 ) */   12 ,
    /* (  0 ,  0 ,  0 ) */   0 , 
    /* (  0 ,  0 ,  1 ) */   12 ,
    /* (  0 ,  1 , -1 ) */   11 ,
    /* (  0 ,  1 ,  0 ) */   10 , 
    /* (  0 ,  1 ,  1 ) */   9 ,
    /* (  1 , -1 , -1 ) */   8 ,
    /* (  1 , -1 ,  0 ) */   7 , 
    /* (  1 , -1 ,  1 ) */   6 ,
    /* (  1 ,  0 , -1 ) */   5 ,
    /* (  1 ,  0 ,  0 ) */   4 , 
    /* (  1 ,  0 ,  1 ) */   3 ,
    /* (  1 ,  1 , -1 ) */   2 ,
    /* (  1 ,  1 ,  0 ) */   1 , 
    /* (  1 ,  1 ,  1 ) */   0 
    };
    
    
/**
 * @brief Get the shift-id of the given pair of cells, swapping them
 *      if need be.
 *
 * @param s The space
 * @param ci Pointer to first #cell.
 * @param cj Pointer second #cell.
 * @param shift Vector from ci to cj.
 *
 * @return The shift ID and set shift, may or may not swap ci and cj.
 */
 
int space_getsid ( struct space *s , struct cell **ci , struct cell **cj , double *shift ) {

    int k, sid = 0;
    struct cell *temp;
    double dx[3];

    /* Get the relative distance between the pairs, wrapping. */
    for ( k = 0 ; k < 3 ; k++ ) {
        dx[k] = (*cj)->loc[k] - (*ci)->loc[k];
        if ( dx[k] < -s->dim[k]/2 )
            shift[k] = s->dim[k];
        else if ( dx[k] > s->dim[k]/2 )
            shift[k] = -s->dim[k];
        else
            shift[k] = 0.0;
        dx[k] += shift[k];
        }
        
    /* Get the sorting index. */
    for ( k = 0 ; k < 3 ; k++ )
        sid = 3*sid + ( (dx[k] < 0.0) ? 0 : ( (dx[k] > 0.0) ? 2 : 1 ) );

    /* Switch the cells around? */
    if ( runner_flip[sid] ) {
        temp = *ci; *ci = *cj; *cj = temp;
        for ( k = 0 ; k < 3 ; k++ )
            shift[k] = -shift[k];
        }
    sid = sortlistID[sid];
    
    /* Return the sort ID. */
    return sid;

    }


/**
 * @brief Recursively dismantle a cell tree.
 *
 */
 
void space_rebuild_recycle ( struct space *s , struct cell *c ) {
    
    int k;
    
    if ( c->split )
        for ( k = 0 ; k < 8 ; k++ )
            if ( c->progeny[k] != NULL ) {
                space_rebuild_recycle( s , c->progeny[k] );
                space_recycle( s , c->progeny[k] );
                c->progeny[k] = NULL;
                }
    
    }

/**
 * @brief Re-build the cells as well as the tasks.
 *
 * @param s The #space in which to update the cells.
 * @param cell_max Maximal cell size.
 *
 */
 
void space_rebuild ( struct space *s , double cell_max ) {

    float h_max = s->cell_min / kernel_gamma, dmin;
    int i, j, k, cdim[3], nr_parts = s->nr_parts;
    struct cell *restrict c;
    struct part *restrict finger, *restrict p, *parts = s->parts;
    int *ind;
    double ih[3], dim[3];
    // ticks tic;
    
    /* Be verbose about this. */
    // printf( "space_rebuild: (re)building space...\n" ); fflush(stdout);
    
    /* Run through the parts and get the current h_max. */
    // tic = getticks();
    if ( s->cells != NULL ) {
        for ( k = 0 ; k < s->nr_cells ; k++ ) {
            if ( s->cells[k].h_max > h_max )
                h_max = s->cells[k].h_max;
            }
        }
    else {
        for ( k = 0 ; k < nr_parts ; k++ ) {
            if ( s->parts[k].h > h_max )
                h_max = s->parts[k].h;
            }
        s->h_max = h_max;
        }
    // printf( "space_rebuild: h_max is %.3e (cell_max=%.3e).\n" , h_max , cell_max );
    // printf( "space_rebuild: getting h_min and h_max took %.3f ms.\n" , (double)(getticks() - tic) / CPU_TPS * 1000 );
    
    /* Get the new putative cell dimensions. */
    for ( k = 0 ; k < 3 ; k++ )
        cdim[k] = floor( s->dim[k] / fmax( h_max*kernel_gamma*space_stretch , cell_max ) );
        
    /* Do we need to re-build the upper-level cells? */
    // tic = getticks();
    if ( s->cells == NULL ||
         cdim[0] < s->cdim[0] || cdim[1] < s->cdim[1] || cdim[2] < s->cdim[2] ) {
    
        /* Free the old cells, if they were allocated. */
        if ( s->cells != NULL ) {
            for ( k = 0 ; k < s->nr_cells ; k++ ) {
                space_rebuild_recycle( s , &s->cells[k] );
                if ( s->cells[k].sort != NULL )
                    free( s->cells[k].sort );
                }
            free( s->cells );
            s->maxdepth = 0;
            }
            
        /* Set the new cell dimensions only if smaller. */
        for ( k = 0 ; k < 3 ; k++ ) {
            s->cdim[k] = cdim[k];
            s->h[k] = s->dim[k] / cdim[k];
            s->ih[k] = 1.0 / s->h[k];
            }
        dmin = fminf( s->h[0] , fminf( s->h[1] , s->h[2] ) );

        /* Allocate the highest level of cells. */
        s->tot_cells = s->nr_cells = cdim[0] * cdim[1] * cdim[2];
        if ( posix_memalign( (void *)&s->cells , 64 , s->nr_cells * sizeof(struct cell) ) != 0 )
            error( "Failed to allocate cells." );
        bzero( s->cells , s->nr_cells * sizeof(struct cell) );
        for ( k = 0 ; k < s->nr_cells ; k++ )
            if ( lock_init( &s->cells[k].lock ) != 0 )
                error( "Failed to init spinlock." );

        /* Set the cell location and sizes. */
        for ( i = 0 ; i < cdim[0] ; i++ )
            for ( j = 0 ; j < cdim[1] ; j++ )
                for ( k = 0 ; k < cdim[2] ; k++ ) {
                    c = &s->cells[ cell_getid( cdim , i , j , k ) ];
                    c->loc[0] = i*s->h[0]; c->loc[1] = j*s->h[1]; c->loc[2] = k*s->h[2];
                    c->h[0] = s->h[0]; c->h[1] = s->h[1]; c->h[2] = s->h[2];
                    c->dmin = dmin;
                    c->depth = 0;
                    c->count = 0;
                    lock_init( &c->lock );
                    }
           
        /* Be verbose about the change. */         
        printf( "space_rebuild: set cell dimensions to [ %i %i %i ].\n" , cdim[0] , cdim[1] , cdim[2] ); fflush(stdout);
                    
        } /* re-build upper-level cells? */
    // printf( "space_rebuild: rebuilding upper-level cells took %.3f ms.\n" , (double)(getticks() - tic) / CPU_TPS * 1000 );
        
    /* Otherwise, just clean up the cells. */
    else {
    
        /* Free the old cells, if they were allocated. */
        for ( k = 0 ; k < s->nr_cells ; k++ ) {
            space_rebuild_recycle( s , &s->cells[k] );
            s->cells[k].sorts = NULL;
            s->cells[k].nr_tasks = 0;
            s->cells[k].nr_density = 0;
            s->cells[k].dx_max = 0.0f;
            s->cells[k].sorted = 0;
            s->cells[k].count = 0;
            s->cells[k].kick1 = NULL;
            s->cells[k].kick2 = NULL;
            }
        s->maxdepth = 0;
    
        }
        
    /* Run through the particles and get their cell index. */
    // tic = getticks();
    if ( ( ind = (int *)malloc( sizeof(int) * s->nr_parts ) ) == NULL )
        error( "Failed to allocate temporary particle indices." );
    ih[0] = s->ih[0]; ih[1] = s->ih[1]; ih[2] = s->ih[2];
    dim[0] = s->dim[0]; dim[1] = s->dim[1]; dim[2] = s->dim[2];
    cdim[0] = s->cdim[0]; cdim[1] = s->cdim[1]; cdim[2] = s->cdim[2];
    #pragma omp parallel for private(p,j)
    for ( k = 0 ; k < nr_parts ; k++ )  {
        p = &parts[k];
        for ( j = 0 ; j < 3 ; j++ )
            if ( p->x[j] < 0.0 )
                p->x[j] += dim[j];
            else if ( p->x[j] >= dim[j] )
                p->x[j] -= dim[j];
        ind[k] = cell_getid( cdim , p->x[0]*ih[0] , p->x[1]*ih[1] , p->x[2]*ih[2] );
        atomic_inc( &s->cells[ ind[k] ].count );
        }
    // printf( "space_rebuild: getting particle indices took %.3f ms.\n" , (double)(getticks() - tic) / CPU_TPS * 1000 );

    /* Sort the parts according to their cells. */
    // tic = getticks();
    parts_sort( parts , ind , s->nr_parts , 0 , s->nr_cells-1 );
    // printf( "space_rebuild: parts_sort took %.3f ms.\n" , (double)(getticks() - tic) / CPU_TPS * 1000 );
    
    /* Verify sort. */
    /* for ( k = 1 ; k < nr_parts ; k++ ) {
        if ( ind[k-1] > ind[k] ) {
            error( "Sort failed!" );
            }
        else if ( ind[k] != cell_getid( cdim , parts[k].x[0]*ih[0] , parts[k].x[1]*ih[1] , parts[k].x[2]*ih[2] ) )
            error( "Incorrect indices!" );
        } */
    
    /* We no longer need the indices as of here. */
    free( ind );    

    /* Hook the cells up to the parts. */
    // tic = getticks();
    finger = s->parts;
    for ( k = 0 ; k < s->nr_cells ; k++ ) {
        c = &s->cells[ k ];
        c->parts = finger;
        finger = &finger[ c->count ];
        }
    // printf( "space_rebuild: hooking up cells took %.3f ms.\n" , (double)(getticks() - tic) / CPU_TPS * 1000 );
        
    /* At this point, we have the upper-level cells, old or new. Now make
       sure that the parts in each cell are ok. */
    // tic = getticks();
    k = 0;
    #pragma omp parallel shared(s,k)
    {
        if ( omp_get_thread_num() < 8 )
            while ( 1 ) {
                int myk = atomic_inc( &k );
                if ( myk < s->nr_cells )
                    space_split( s , &s->cells[myk] );
                else
                    break;
                }
        }
    // printf( "space_rebuild: space_split took %.3f ms.\n" , (double)(getticks() - tic) / CPU_TPS * 1000 );
        
    }


/**
 * @brief Sort the particles and condensed particles according to the given indices.
 *
 * @param parts The list of #part
 * @param ind The indices with respect to which the parts are sorted.
 * @param N The number of parts
 * @param min Lowest index.
 * @param max highest index.
 */
 
void parts_sort ( struct part *parts , int *ind , int N , int min , int max ) {

    struct {
        int i, j, min, max, ready;
        } qstack[space_qstack];
    volatile int first, last, waiting;
    
    int pivot;
    int i, ii, j, jj, temp_i, qid;
    struct part temp_p;
    
    /* Init the interval stack. */
    qstack[0].i = 0;
    qstack[0].j = N-1;
    qstack[0].min = min;
    qstack[0].max = max;
    qstack[0].ready = 1;
    for ( i = 1 ; i < space_qstack ; i++ )
        qstack[i].ready = 0;
    first = 0; last = 1; waiting = 1;
    
    /* Parallel bit. */
    #pragma omp parallel default(shared) private(pivot,i,ii,j,jj,min,max,temp_i,qid,temp_p)
    {
    
        /* Main loop. */
        if ( omp_get_thread_num() < 8 )
        while ( waiting > 0 ) {
        
            /* Grab an interval off the queue. */
            qid = atomic_inc( &first ) % space_qstack;
            
            /* Wait for the interval to be ready. */
            while ( waiting > 0 && atomic_cas( &qstack[qid].ready , 1 , 0 ) != 1 );
            
            /* Broke loop for all the wrong reasons? */
            if ( waiting == 0 )
                break;
        
            /* Get the stack entry. */
            i = qstack[qid].i;
            j = qstack[qid].j;
            min = qstack[qid].min;
            max = qstack[qid].max;
            // printf( "parts_sort_par: thread %i got interval [%i,%i] with values in [%i,%i].\n" , omp_get_thread_num() , i , j , min , max );
            
            /* Loop over sub-intervals. */
            while ( 1 ) {
            
                /* Bring beer. */
                pivot = (min + max) / 2;
                
                /* One pass of QuickSort's partitioning. */
                ii = i; jj = j;
                while ( ii < jj ) {
                    while ( ii <= j && ind[ii] <= pivot )
                        ii++;
                    while ( jj >= i && ind[jj] > pivot )
                        jj--;
                    if ( ii < jj ) {
                        temp_i = ind[ii]; ind[ii] = ind[jj]; ind[jj] = temp_i;
                        temp_p = parts[ii]; parts[ii] = parts[jj]; parts[jj] = temp_p;
                        }
                    }

                /* Verify sort. */
                /* for ( int k = i ; k <= jj ; k++ )
                    if ( ind[k] > pivot ) {
                        printf( "parts_sort: sorting failed at k=%i, ind[k]=%i, pivot=%i, i=%i, j=%i, N=%i.\n" , k , ind[k] , pivot , i , j , N );
                        error( "Partition failed (<=pivot)." );
                        }
                for ( int k = jj+1 ; k <= j ; k++ )
                    if ( ind[k] <= pivot ) {
                        printf( "parts_sort: sorting failed at k=%i, ind[k]=%i, pivot=%i, i=%i, j=%i, N=%i.\n" , k , ind[k] , pivot , i , j , N );
                        error( "Partition failed (>pivot)." );
                        } */
                        
                /* Split-off largest interval. */
                if ( jj - i > j - jj+1 ) {

                    /* Recurse on the left? */
                    if ( jj > i  && pivot > min ) {
                        qid = atomic_inc( &last ) % space_qstack;
                        qstack[qid].i = i;
                        qstack[qid].j = jj;
                        qstack[qid].min = min;
                        qstack[qid].max = pivot;
                        qstack[qid].ready = 1;
                        atomic_inc( &waiting );
                        }

                    /* Recurse on the right? */
                    if ( jj+1 < j && pivot+1 < max ) {
                        i = jj+1;
                        min = pivot+1;
                        }
                    else
                        break;
                        
                    }
                    
                else {
                
                    /* Recurse on the right? */
                    if ( jj+1 < j && pivot+1 < max ) {
                        qid = atomic_inc( &last ) % space_qstack;
                        qstack[qid].i = jj+1;
                        qstack[qid].j = j;
                        qstack[qid].min = pivot+1;
                        qstack[qid].max = max;
                        qstack[qid].ready = 1;
                        atomic_inc( &waiting );
                        }
                        
                    /* Recurse on the left? */
                    if ( jj > i  && pivot > min ) {
                        j = jj;
                        max = pivot;
                        }
                    else
                        break;

                    }
                    
                } /* loop over sub-intervals. */
    
            atomic_dec( &waiting );

            } /* main loop. */
    
        } /* parallel bit. */
    
    /* Verify sort. */
    /* for ( i = 1 ; i < N ; i++ )
        if ( ind[i-1] > ind[i] )
            error( "Sorting failed!" ); */

    }


/**
 * @brief Mapping function to free the sorted indices buffers.
 */

void space_map_clearsort ( struct cell *c , void *data ) {

    if ( c->sort != NULL ) {
        free( c->sort );
        c->sort = NULL;
        }

    }


/**
 * @brief Map a function to all particles in a aspace.
 *
 * @param s The #space we are working in.
 * @param fun Function pointer to apply on the cells.
 * @param data Data passed to the function fun.
 */
 
void space_map_parts ( struct space *s , void (*fun)( struct part *p , struct cell *c , void *data ) , void *data ) {

    int cid = 0;

    void rec_map ( struct cell *c ) {
    
        int k;
        
        /* No progeny? */
        if ( !c->split )
            for ( k = 0 ; k < c->count ; k++ )
                fun( &c->parts[k] , c , data );
                
        /* Otherwise, recurse. */
        else
            for ( k = 0 ; k < 8 ; k++ )
                if ( c->progeny[k] != NULL )
                    rec_map( c->progeny[k] );
                
        }
        
    /* Call the recursive function on all higher-level cells. */
    #pragma omp parallel shared(cid)
    {
        int mycid;
        while ( 1 ) {
            #pragma omp critical
            mycid = cid++;
            if ( mycid < s->nr_cells )
                rec_map( &s->cells[mycid] );
            else
                break;
            }
        }

    }


/**
 * @brief Map a function to all particles in a aspace.
 *
 * @param s The #space we are working in.
 * @param full Map to all cells, including cells with sub-cells.
 * @param fun Function pointer to apply on the cells.
 * @param data Data passed to the function fun.
 */
 
void space_map_cells_post ( struct space *s , int full , void (*fun)( struct cell *c , void *data ) , void *data ) {

    int cid = 0;

    void rec_map ( struct cell *c ) {
    
        int k;
        
        /* Recurse. */
        if ( c->split )
            for ( k = 0 ; k < 8 ; k++ )
                if ( c->progeny[k] != NULL )
                    rec_map( c->progeny[k] );
                
        /* No progeny? */
        if ( full || !c->split )
            fun( c , data );
                
        }
        
    /* Call the recursive function on all higher-level cells. */
    // #pragma omp parallel shared(s,cid)
    {
        int mycid;
        while ( 1 ) {
            // #pragma omp critical
            mycid = cid++;
            if ( mycid < s->nr_cells )
                rec_map( &s->cells[mycid] );
            else
                break;
            }
        }

    }


void space_map_cells_pre ( struct space *s , int full , void (*fun)( struct cell *c , void *data ) , void *data ) {

    int cid = 0;

    void rec_map ( struct cell *c ) {
    
        int k;
        
        /* No progeny? */
        if ( full || !c->split )
            fun( c , data );
                
        /* Recurse. */
        if ( c->split )
            for ( k = 0 ; k < 8 ; k++ )
                if ( c->progeny[k] != NULL )
                    rec_map( c->progeny[k] );
                
        }
        
    /* Call the recursive function on all higher-level cells. */
    // #pragma omp parallel shared(s,cid)
    {
        int mycid;
        while ( 1 ) {
            // #pragma omp critical
            mycid = cid++;
            if ( mycid < s->nr_cells )
                rec_map( &s->cells[mycid] );
            else
                break;
            }
        }

    }


/**
 * @brief Split cells that contain too many particles.
 *
 * @param s The #space we are working in.
 * @param c The #cell under consideration.
 */
 
void space_split ( struct space *s , struct cell *c ) {

    int k, count = c->count, maxdepth = 0;
    float h, h_max = 0.0f, dt, dt_min = c->parts[0].dt, dt_max = dt_min;
    struct cell *temp;
    struct part *p, *parts = c->parts;
    struct xpart *xp;
    
    /* Check the depth. */
    if ( c->depth > s->maxdepth )
        s->maxdepth = c->depth;
    
    /* Split or let it be? */
    if ( count > space_splitsize ) {
    
        /* No longer just a leaf. */
        c->split = 1;
        
        /* Create the cell's progeny. */
        for ( k = 0 ; k < 8 ; k++ ) {
            temp = space_getcell( s );
            temp->count = 0;
            temp->loc[0] = c->loc[0];
            temp->loc[1] = c->loc[1];
            temp->loc[2] = c->loc[2];
            temp->h[0] = c->h[0]/2;
            temp->h[1] = c->h[1]/2;
            temp->h[2] = c->h[2]/2;
            temp->dmin = c->dmin/2;
            if ( k & 4 )
                temp->loc[0] += temp->h[0];
            if ( k & 2 )
                temp->loc[1] += temp->h[1];
            if ( k & 1 )
                temp->loc[2] += temp->h[2];
            temp->depth = c->depth + 1;
            temp->split = 0;
            temp->h_max = 0.0;
            temp->dx_max = 0.0;
            temp->parent = c;
            c->progeny[k] = temp;
            }
            
        /* Split the cell data. */
        cell_split( c );
            
        /* Remove any progeny with zero parts. */
        for ( k = 0 ; k < 8 ; k++ )
            if ( c->progeny[k]->count == 0 ) {
                space_recycle( s , c->progeny[k] );
                c->progeny[k] = NULL;
                }
            else {
                space_split( s , c->progeny[k] );
                h_max = fmaxf( h_max , c->progeny[k]->h_max );
                dt_min = fminf( dt_min , c->progeny[k]->dt_min );
                dt_max = fmaxf( dt_max , c->progeny[k]->dt_max );
                if ( c->progeny[k]->maxdepth > maxdepth )
                    maxdepth = c->progeny[k]->maxdepth;
                }
                
        /* Set the values for this cell. */
        c->h_max = h_max;
        c->dt_min = dt_min;
        c->dt_max = dt_max;
        c->maxdepth = maxdepth;
                
        }
        
    /* Otherwise, collect the data for this cell. */
    else {
    
        /* Clear the progeny. */
        bzero( c->progeny , sizeof(struct cell *) * 8 );
        c->split = 0;
        c->maxdepth = c->depth;
        
        /* Get dt_min/dt_max. */
        
        for ( k = 0 ; k < count ; k++ ) {
            p = &parts[k];
            xp = p->xtras;
            xp->x_old[0] = p->x[0];
            xp->x_old[1] = p->x[1];
            xp->x_old[2] = p->x[2];
            dt = p->dt;
            h = p->h;
            if ( h > h_max )
                h_max = h;
            if ( dt < dt_min )
                dt_min = dt;
            if ( dt > dt_max )
                dt_max = dt;
            }
        c->h_max = h_max;
        c->dt_min = dt_min;
        c->dt_max = dt_max;
            
        }
        
    /* Set ownership accorind to the start of the parts array. */
    c->owner = ( c->parts - s->parts ) * s->nr_queues / s->nr_parts;

    }


/**
 * @brief Return a used cell to the cell buffer.
 *
 * @param s The #space.
 * @param c The #cell.
 */
 
void space_recycle ( struct space *s , struct cell *c ) {

    /* Lock the space. */
    lock_lock( &s->lock );
    
    /* Clear the cell. */
    if ( lock_destroy( &c->lock ) != 0 )
        error( "Failed to destroy spinlock." );
        
    /* Clear this cell's sort arrays. */
    if ( c->sort != NULL )
        free( c->sort );
        
    /* Clear the cell data. */
    bzero( c , sizeof(struct cell) );
    
    /* Hook this cell into the buffer. */
    c->next = s->cells_new;
    s->cells_new = c;
    s->tot_cells -= 1;
    
    /* Unlock the space. */
    lock_unlock_blind( &s->lock );
    
    }


/**
 * @brief Get a new empty cell.
 *
 * @param s The #space.
 */
 
struct cell *space_getcell ( struct space *s ) {

    struct cell *c;
    int k;
    
    /* Lock the space. */
    lock_lock( &s->lock );
    
    /* Is the buffer empty? */
    if ( s->cells_new == NULL ) {
        if ( posix_memalign( (void *)&s->cells_new , 64 , space_cellallocchunk * sizeof(struct cell) ) != 0 )
            error( "Failed to allocate more cells." );
        bzero( s->cells_new , space_cellallocchunk * sizeof(struct cell) );
        for ( k = 0 ; k < space_cellallocchunk-1 ; k++ )
            s->cells_new[k].next = &s->cells_new[k+1];
        s->cells_new[ space_cellallocchunk-1 ].next = NULL;
        }

    /* Pick off the next cell. */
    c = s->cells_new;
    s->cells_new = c->next;
    s->tot_cells += 1;
    
    /* Unlock the space. */
    lock_unlock_blind( &s->lock );
    
    /* Init some things in the cell. */
    c->sorts = NULL;
    c->nr_tasks = 0;
    c->nr_density = 0;
    c->dx_max = 0.0f;
    c->sorted = 0;
    c->count = 0;
    c->kick1 = NULL;
    c->kick2 = NULL;
    if ( lock_init( &c->lock ) != 0 )
        error( "Failed to initialize cell spinlock." );
    c->owner = -1;
        
    return c;

    }


/**
 * @brief Split the space into cells given the array of particles.
 *
 * @param s The #space to initialize.
 * @param dim Spatial dimensions of the domain.
 * @param parts Pointer to an array of #part.
 * @param N The number of parts in the space.
 * @param periodic flag whether the domain is periodic or not.
 * @param h_max The maximal interaction radius.
 *
 * Makes a grid of edge length > r_max and fills the particles
 * into the respective cells. Cells containing more than #space_maxppc
 * parts with a cutoff below half the cell width are then split
 * recursively.
 */


void space_init ( struct space *s , double dim[3] , struct part *parts , int N , int periodic , double h_max ) {

    int k;

    /* Store eveything in the space. */
    s->dim[0] = dim[0]; s->dim[1] = dim[1]; s->dim[2] = dim[2];
    s->periodic = periodic;
    s->nr_parts = N;
    s->parts = parts;
    s->cell_min = h_max;
    s->nr_queues = 1;
    
    /* Allocate and link the xtra parts array. */
    if ( posix_memalign( (void *)&s->xparts , 32 , N * sizeof(struct xpart) ) != 0 )
        error( "Failed to allocate xparts." );
    for ( k = 0 ; k < N ; k++ )
        s->parts[k].xtras = &s->xparts[k];
        
    /* Init the space lock. */
    if ( lock_init( &s->lock ) != 0 )
        error( "Failed to create space spin-lock." );
    
    /* Build the cells and the tasks. */
    space_rebuild( s , h_max );
        
    }

