#include <stdio.h>

#include "particles/ParticleSolver.hpp"
#include "visit/VisitWriter.hpp"



namespace minicombust::particles 
{
    using namespace minicombust::visit;

    template<class T>
    void ParticleSolver<T>::output_data(uint64_t timestep)
    {
        // COMMENTED SECTION FOR CLUSTERING PARTICLES TO MESH POINTS
        // mesh->clear_particles_per_point_array();

        // // Assign each particles to one of the vertexes of the bounding cell.
        // for(uint64_t p = 0; p < particles.size(); p++)  // Iterate through each particle
        // {
        //     Particle<T> *particle     = &particles[p];
        //     if ( particle->decayed )  continue;

        //     double closest_dist       = __DBL_MAX__;
        //     uint64_t closest_vertex   = UINT64_MAX;
        //     for (uint64_t i = 0; i < mesh->cell_size; i++)  // Iterate through the points of the cell that the particle is in
        //     {
        //         const uint64_t point_index = mesh->cells[particle->cell*mesh->cell_size + i];
        //         const double dist = magnitude(particle->x1 - mesh->points[point_index]);
        //         if ( dist < closest_dist )
        //         {
        //             closest_dist   = dist;
        //             closest_vertex = point_index;
        //         }
        //     }
        //     mesh->particles_per_point[closest_vertex] += 1;
        // }

        VisitWriter<double> *vtk_writer = new VisitWriter<double>(mesh);
        vtk_writer->write_particles("minicombust", timestep, particles);
    }

    template<class T>
    void ParticleSolver<T>::print_logger_stats(uint64_t timesteps, double runtime)
    {
        Particle_Logger loggers[mpi_config->particle_flow_world_size];
        MPI_Gather(&logger, sizeof(Particle_Logger), MPI_BYTE, &loggers, sizeof(Particle_Logger), MPI_BYTE, 0, mpi_config->particle_flow_world);
        
        memset(&logger,           0, sizeof(Particle_Logger));
        for (int rank = 0; rank < mpi_config->particle_flow_world_size; rank++)
        {
            logger.num_particles            += loggers[rank].num_particles;
            logger.avg_particles            += loggers[rank].avg_particles;
            logger.emitted_particles        += loggers[rank].emitted_particles;
            logger.cell_checks              += loggers[rank].cell_checks;
            logger.position_adjustments     += loggers[rank].position_adjustments;
            logger.lost_particles           += loggers[rank].lost_particles;
            logger.boundary_intersections   += loggers[rank].boundary_intersections;
            logger.decayed_particles        += loggers[rank].decayed_particles;
            logger.burnt_particles          += loggers[rank].burnt_particles;
            logger.breakups                 += loggers[rank].breakups;
            logger.interpolated_cells       += loggers[rank].interpolated_cells / (double)mpi_config->particle_flow_world_size;
        }

        if (mpi_config->rank == 0)
        {
            cout << "Particle Solver Stats:                         " << endl;
            cout << "\tParticles:                                   " << ((double)logger.num_particles)                                                                   << endl;
            cout << "\tParticles (per iter):                        " << particle_dist->particles_per_timestep*mpi_config->particle_flow_world_size                       << endl;
            cout << "\tEmitted Particles:                           " << logger.emitted_particles                                                                         << endl;
            cout << "\tAvg Particles (per iter):                    " << logger.avg_particles                                                                             << endl;
            cout << endl;
            cout << "\tCell checks:                                 " << ((double)logger.cell_checks)                                                                     << endl;
            cout << "\tCell checks (per iter):                      " << ((double)logger.cell_checks) / timesteps                                                         << endl;
            cout << "\tCell checks (per particle, per iter):        " << ((double)logger.cell_checks) / (((double)logger.num_particles)*timesteps)                        << endl;
            cout << endl;
            cout << "\tEdge adjustments:                            " << ((double)logger.position_adjustments)                                                            << endl;
            cout << "\tEdge adjustments (per iter):                 " << ((double)logger.position_adjustments) / timesteps                                                << endl;
            cout << "\tEdge adjustments (per particle, per iter):   " << ((double)logger.position_adjustments) / (((double)logger.num_particles)*timesteps)               << endl;
            cout << "\tLost Particles:                              " << ((double)logger.lost_particles      )                                                            << endl;
            cout << endl;
            cout << "\tBoundary Intersections:                      " << ((double)logger.boundary_intersections)                                                          << endl;
            cout << "\tDecayed Particles:                           " << round(10000.*(((double)logger.decayed_particles) / ((double)logger.num_particles)))/100. << "% " << endl;
            cout << "\tBurnt Particles:                             " << ((double)logger.burnt_particles)                                                                 << endl;
            cout << "\tBreakups:                                    " << ((double)logger.breakups)                                                                        << endl;
            cout << "\tBreakup Age:                                 " << ((double)logger.breakup_age)                                                                     << endl;
            cout << endl;
            cout << "\tInterpolated Cells (per rank):               " << ((double)logger.interpolated_cells)                                                              << endl;
            cout << "\tInterpolated Cells Percentage (per rank):    " << round(10000.*(((double)logger.interpolated_cells) / ((double)mesh->mesh_size)))/100. << "% "     << endl;

            cout << endl;
        }
        performance_logger.print_counters(mpi_config->rank, mpi_config->world_size, runtime);
    }


    template<class T> 
    void ParticleSolver<T>::update_flow_field(bool send_particle)
    {
        performance_logger.my_papi_start();

        const int cell_size = cell_particle_field_map.size();
        resize_cells_arrays(cell_size);
        
        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: update_flow_field.\n");
        int flow_rank = mpi_config->particle_flow_world_size;

        uint64_t count = 0;
        for (auto& cell_it: cell_particle_field_map)
        {
            uint64_t cell = cell_it.first;
            // cell_indexes[count++] = cell;

            // Get 9 cells neighbours below
            const uint64_t below_neighbour                = mesh->cell_neighbours[cell*mesh->faces_per_cell                   + DOWN_FACE];
            const uint64_t below_left_neighbour           = mesh->cell_neighbours[below_neighbour*mesh->faces_per_cell        + LEFT_FACE];
            const uint64_t below_right_neighbour          = mesh->cell_neighbours[below_neighbour*mesh->faces_per_cell        + RIGHT_FACE];
            const uint64_t below_front_neighbour          = mesh->cell_neighbours[below_neighbour*mesh->faces_per_cell        + FRONT_FACE];
            const uint64_t below_back_neighbour           = mesh->cell_neighbours[below_neighbour*mesh->faces_per_cell        + BACK_FACE];
            const uint64_t below_left_front_neighbour     = mesh->cell_neighbours[below_left_neighbour*mesh->faces_per_cell   + FRONT_FACE];
            const uint64_t below_left_back_neighbour      = mesh->cell_neighbours[below_left_neighbour*mesh->faces_per_cell   + BACK_FACE];
            const uint64_t below_right_front_neighbour    = mesh->cell_neighbours[below_right_neighbour*mesh->faces_per_cell  + FRONT_FACE];
            const uint64_t below_right_back_neighbour     = mesh->cell_neighbours[below_right_neighbour*mesh->faces_per_cell  + BACK_FACE];

            // Get 9 cells neighbours above
            const uint64_t above_neighbour                = mesh->cell_neighbours[cell*mesh->faces_per_cell                   + UP_FACE];
            const uint64_t above_left_neighbour           = mesh->cell_neighbours[above_neighbour*mesh->faces_per_cell        + LEFT_FACE];
            const uint64_t above_right_neighbour          = mesh->cell_neighbours[above_neighbour*mesh->faces_per_cell        + RIGHT_FACE];
            const uint64_t above_front_neighbour          = mesh->cell_neighbours[above_neighbour*mesh->faces_per_cell        + FRONT_FACE];
            const uint64_t above_back_neighbour           = mesh->cell_neighbours[above_neighbour*mesh->faces_per_cell        + BACK_FACE];
            const uint64_t above_left_front_neighbour     = mesh->cell_neighbours[above_left_neighbour*mesh->faces_per_cell   + FRONT_FACE];
            const uint64_t above_left_back_neighbour      = mesh->cell_neighbours[above_left_neighbour*mesh->faces_per_cell   + BACK_FACE];
            const uint64_t above_right_front_neighbour    = mesh->cell_neighbours[above_right_neighbour*mesh->faces_per_cell  + FRONT_FACE];
            const uint64_t above_right_back_neighbour     = mesh->cell_neighbours[above_right_neighbour*mesh->faces_per_cell  + BACK_FACE];

            // Get 8 cells neighbours around
            const uint64_t around_left_neighbour          = mesh->cell_neighbours[cell*mesh->faces_per_cell                   + LEFT_FACE];
            const uint64_t around_right_neighbour         = mesh->cell_neighbours[cell*mesh->faces_per_cell                   + RIGHT_FACE];
            const uint64_t around_front_neighbour         = mesh->cell_neighbours[cell*mesh->faces_per_cell                   + FRONT_FACE];
            const uint64_t around_back_neighbour          = mesh->cell_neighbours[cell*mesh->faces_per_cell                   + BACK_FACE];
            const uint64_t around_left_front_neighbour    = mesh->cell_neighbours[around_left_neighbour*mesh->faces_per_cell  + FRONT_FACE];
            const uint64_t around_left_back_neighbour     = mesh->cell_neighbours[around_left_neighbour*mesh->faces_per_cell  + BACK_FACE];
            const uint64_t around_right_front_neighbour   = mesh->cell_neighbours[around_right_neighbour*mesh->faces_per_cell + FRONT_FACE];
            const uint64_t around_right_back_neighbour    = mesh->cell_neighbours[around_right_neighbour*mesh->faces_per_cell + BACK_FACE];

            neighbours_set.insert(cell); 

            // Get 9 cells neighbours below
            neighbours_set.insert(below_neighbour);                
            neighbours_set.insert(below_left_neighbour);           
            neighbours_set.insert(below_right_neighbour);          
            neighbours_set.insert(below_front_neighbour);          
            neighbours_set.insert(below_back_neighbour);           
            neighbours_set.insert(below_left_front_neighbour);     
            neighbours_set.insert(below_left_back_neighbour);      
            neighbours_set.insert(below_right_front_neighbour);    
            neighbours_set.insert(below_right_back_neighbour);     

            // Get 9 cells neighbours above
            neighbours_set.insert(above_neighbour);                
            neighbours_set.insert(above_left_neighbour);           
            neighbours_set.insert(above_right_neighbour);          
            neighbours_set.insert(above_front_neighbour);          
            neighbours_set.insert(above_back_neighbour);           
            neighbours_set.insert(above_left_front_neighbour);     
            neighbours_set.insert(above_left_back_neighbour);      
            neighbours_set.insert(above_right_front_neighbour);    
            neighbours_set.insert(above_right_back_neighbour);     

            // Get 8 cells neighbours around
            neighbours_set.insert(around_left_neighbour);          
            neighbours_set.insert(around_right_neighbour);         
            neighbours_set.insert(around_front_neighbour);         
            neighbours_set.insert(around_back_neighbour);          
            neighbours_set.insert(around_left_front_neighbour);    
            neighbours_set.insert(around_left_back_neighbour);     
            neighbours_set.insert(around_right_front_neighbour);   
            neighbours_set.insert(around_right_back_neighbour); 
        }

        count = 0;
        neighbours_set.erase(MESH_BOUNDARY);
        for (auto& cell: neighbours_set)
        {
            cell_indexes[count++] = cell;
        }
        neighbours_size                = neighbours_set.size();
        resize_cells_arrays(neighbours_size);
        MPI_Barrier(mpi_config->world);

        // Send local neighbours size
        MPI_GatherSet ( mpi_config, neighbours_set, cell_indexes );

        // Get reduced neighbours size
        MPI_Bcast(&neighbours_size,                1, MPI_INT, flow_rank, mpi_config->world);

        rank_neighbours_size = neighbours_size / mpi_config->particle_flow_world_size;
        if (mpi_config->particle_flow_rank < ((int)neighbours_size % mpi_config->particle_flow_world_size))
            rank_neighbours_size++;
        resize_cells_arrays(rank_neighbours_size);

        //Get local portion of neighbour cells and cell fields
        MPI_Request scatter_requests[3];
        MPI_Iscatterv(NULL, NULL, NULL, MPI_UINT64_T,                   cell_indexes,       rank_neighbours_size, MPI_UINT64_T,                   flow_rank, mpi_config->world, &scatter_requests[0]);
        MPI_Iscatterv(NULL, NULL, NULL, mpi_config->MPI_FLOW_STRUCTURE, cell_flow_aos,      rank_neighbours_size, mpi_config->MPI_FLOW_STRUCTURE, flow_rank, mpi_config->world, &scatter_requests[1]);
        MPI_Iscatterv(NULL, NULL, NULL, mpi_config->MPI_FLOW_STRUCTURE, cell_flow_grad_aos, rank_neighbours_size, mpi_config->MPI_FLOW_STRUCTURE, flow_rank, mpi_config->world, &scatter_requests[2]);
        
        // Write local particle fields to array
        count = 0;
        cell_particle_field_map.erase(MESH_BOUNDARY);
        for (auto& cell_it: cell_particle_field_map)
            cell_particle_aos[count++]        = cell_it.second;

        node_to_position_map.clear(); 
        cell_particle_field_map.clear();
        neighbours_set.clear();

        logger.interpolated_cells += ((float) neighbours_size) / ((float)num_timesteps);

        MPI_Waitall(3, scatter_requests, MPI_STATUSES_IGNORE);
        
        if (send_particle)
        {
            MPI_GatherSet (mpi_config, cell_particle_field_map, cell_particle_aos);
        }

        performance_logger.my_papi_stop(performance_logger.update_flow_field_event_counts, &performance_logger.update_flow_field_time);
    }
            
    template<class T> 
    void ParticleSolver<T>::particle_release()
    {
        performance_logger.my_papi_start();

        // TODO: Reuse decaying particle space
        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: particle_release.\n");
        particle_dist->emit_particles(particles, cell_particle_field_map, particle_nodes_set, &logger);

        performance_logger.my_papi_stop(performance_logger.emit_event_counts, &performance_logger.emit_time);
    }

    template<class T> 
    void ParticleSolver<T>::solve_spray_equations()
    {
        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: solve_spray_equations.\n");

        const uint64_t cell_size       = mesh->cell_size; 

        const uint64_t particles_size  = particles.size(); 

        performance_logger.my_papi_start();

        // unordered_set<uint64_t> REMOVE_TEST_num_points;

        // Solve spray equations
        #pragma ivdep
        for (uint64_t p = 0; p < particles_size; p++)
        {
            vec<T> total_vector_weight   = {0.0, 0.0, 0.0};
            T total_scalar_weight        = 0.0;

            vec<T> interp_gas_vel = {0.0, 0.0, 0.0};
            T interp_gas_pre      = 0.0;
            T interp_gas_tem      = 0.0;
            
            #pragma ivdep
            for (uint64_t n = 0; n < cell_size; n++)
            {
                const uint64_t node           = mesh->cells[particles[p].cell*cell_size + n]; 
                const vec<T> node_to_particle = particles[p].x1 - mesh->points[mesh->cells[particles[p].cell*cell_size + n]];

                // REMOVE_TEST_num_points.insert(node);

                vec<T> weight      = 1.0 / (node_to_particle * node_to_particle);
                T weight_magnitude = magnitude(weight);

                total_vector_weight   += weight;
                total_scalar_weight   += weight_magnitude;
                interp_gas_vel        += weight           * all_interp_node_flow_fields[node_to_position_map[node]].vel;
                interp_gas_pre        += weight_magnitude * all_interp_node_flow_fields[node_to_position_map[node]].pressure;
                interp_gas_tem        += weight_magnitude * all_interp_node_flow_fields[node_to_position_map[node]].temp;
            }

            particles[p].gas_vel           = interp_gas_vel / total_vector_weight;
            particles[p].gas_pressure      = interp_gas_pre / total_scalar_weight;
            particles[p].gas_temperature   = interp_gas_tem / total_scalar_weight;
        }

        // static uint64_t node_avg = 0;
        static uint64_t timestep_counter = 0;
        timestep_counter++;
        // node_avg += REMOVE_TEST_num_points.size();
        // if (mpi_config->particle_flow_rank == 0)  printf("INTERP_PART Time %d, nodes_fields_used %d\n", timestep_counter-1, REMOVE_TEST_num_points.size());

        // if (timestep_counter == 1500 && mpi_config->rank == 0)
        // {
        //     printf("NODES INTERPOLATED %f\n", ((double)node_avg) / 1500.);
        // }

        performance_logger.my_papi_stop(performance_logger.particle_interpolation_event_counts, &performance_logger.particle_interpolation_time);
        performance_logger.my_papi_start();

        vector<uint64_t> decayed_particles;
        #pragma ivdep
        for (uint64_t p = 0; p < particles_size; p++)
        {
            particles[p].solve_spray( delta, &logger, particles );

            if (particles[p].decayed)  decayed_particles.push_back(p);
        }

        const uint64_t decayed_particles_size = decayed_particles.size();
        #pragma ivdep
        for (int128_t i = decayed_particles_size - 1; i >= 0; i--)
        {
            particles[decayed_particles[i]] = particles.back();
            particles.pop_back();
        }


        performance_logger.my_papi_stop(performance_logger.spray_kernel_event_counts, &performance_logger.spray_time);
    }

    template<class T> 
    void ParticleSolver<T>::update_particle_positions()
    {
        performance_logger.my_papi_start();

        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: update_particle_positions.\n");
        const uint64_t particles_size  = particles.size();

        // Update particle positions
        vector<uint64_t> decayed_particles;
        #pragma ivdep
        for (uint64_t p = 0; p < particles_size; p++)
        {   
            // Check if particle is in the current cell. Tetras = Volume/Area comparison method. https://www.peertechzpublications.com/articles/TCSIT-6-132.php.
            particles[p].update_cell(mesh, &logger);

            if (particles[p].decayed)  decayed_particles.push_back(p);
            else
            {
                cell_particle_field_map[particles[p].cell].cell      = particles[p].cell;
                cell_particle_field_map[particles[p].cell].momentum += particles[p].particle_cell_fields.momentum;
                cell_particle_field_map[particles[p].cell].energy   += particles[p].particle_cell_fields.energy;
                cell_particle_field_map[particles[p].cell].fuel     += particles[p].particle_cell_fields.fuel;

                for (uint64_t i = 0; i < mesh->cell_size; i++)
                    particle_nodes_set.insert(mesh->cells[particles[p].cell*mesh->cell_size + i]);
            }
        }
        // if (mpi_config->particle_flow_rank == 0)  printf("UPDATE_POSI nodes_fields_updated %d \n", particle_nodes_set.size());


        const uint64_t decayed_particles_size = decayed_particles.size();
        #pragma ivdep
        for (int128_t i = decayed_particles_size - 1; i >= 0; i--)
        {
            particles[decayed_particles[i]] = particles.back();
            particles.pop_back();
        }

        performance_logger.my_papi_stop(performance_logger.position_kernel_event_counts, &performance_logger.position_time);
    }

    template<class T>
    void ParticleSolver<T>::update_spray_source_terms()
    {
        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: update_spray_source_terms.\n");
    }

    template<class T> 
    void ParticleSolver<T>::map_source_terms_to_grid()
    {
        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: map_source_terms_to_grid.\n");
    }

    template<class T> 
    void ParticleSolver<T>::interpolate_nodal_data()
    {
        performance_logger.my_papi_start();

        if (PARTICLE_SOLVER_DEBUG)  printf("\tRunning fn: interpolate_data.\n");

        const uint64_t cell_size  = mesh->cell_size; 

        const T node_neighbours       = 8; // Cube specific

        // Faster if particles in some of the cells
        static int time = 0;
        static double time_stats0 = 0;
        static double time_stats1 = 0;
        static double time_stats3 = 0;
        static double time_stats4 = 0;
        static double time_stats45 = 0;
        static double time_stats5 = 0;

        time++;


        time_stats1 -= MPI_Wtime();

        time_stats1 += MPI_Wtime();
        time_stats3 -= MPI_Wtime();


        uint64_t local_nodes_size = 0;

        #pragma ivdep
        for (uint64_t i = 0; i < rank_neighbours_size; i++)
        {
            const uint64_t c = cell_indexes[i];

            const uint64_t *cell             = mesh->cells + c*cell_size;
            const vec<T> cell_centre         = mesh->cell_centres[c];

            const flow_aos<T> flow_term      = cell_flow_aos[i];      
            const flow_aos<T> flow_grad_term = cell_flow_grad_aos[i]; 

            #pragma ivdep
            for (uint64_t n = 0; n < cell_size; n++)
            {
                const uint64_t node_id = cell[n];
                const vec<T> direction             = mesh->points[node_id] - cell_centre;


                if (node_to_position_map.contains(node_id))
                {
                    all_interp_node_flow_fields[node_to_position_map[node_id]].vel      += (flow_term.vel      + dot_product(flow_grad_term.vel,      direction)) / node_neighbours;
                    all_interp_node_flow_fields[node_to_position_map[node_id]].pressure += (flow_term.pressure + dot_product(flow_grad_term.pressure, direction)) / node_neighbours;
                    all_interp_node_flow_fields[node_to_position_map[node_id]].temp     += (flow_term.temp     + dot_product(flow_grad_term.temp,     direction)) / node_neighbours;
                }
                else
                {

                    const T boundary_neighbours = node_neighbours - mesh->cells_per_point[node_id]; 

                    node_to_position_map[node_id] = local_nodes_size;
                    
                    flow_aos<T> temp_term;
                    temp_term.vel      = ((mesh->dummy_gas_vel * boundary_neighbours) + flow_term.vel      + dot_product(flow_grad_term.vel,      direction)) / node_neighbours;
                    temp_term.pressure = ((mesh->dummy_gas_pre * boundary_neighbours) + flow_term.pressure + dot_product(flow_grad_term.pressure, direction)) / node_neighbours;
                    temp_term.temp     = ((mesh->dummy_gas_tem * boundary_neighbours) + flow_term.temp     + dot_product(flow_grad_term.temp,     direction)) / node_neighbours;
                    
                    all_interp_node_indexes[local_nodes_size]     = node_id;
                    all_interp_node_flow_fields[local_nodes_size] = temp_term;

                    local_nodes_size++;
                }
            }
        }

        time_stats3 += MPI_Wtime();
        time_stats4 -= MPI_Wtime();
        
        rank_nodal_sizes[mpi_config->particle_flow_rank] = local_nodes_size;

        static uint64_t node_avg = 0;
        node_avg += node_to_position_map.size();

        // Send indexes to everyone
        time_stats4  += MPI_Wtime();
        time_stats45 -= MPI_Wtime();

        const uint64_t rank = mpi_config->particle_flow_rank;

        int max_levels = 1;
        while (max_levels < mpi_config->particle_flow_world_size)
            max_levels *= 2;

        bool have_data = true;
        for ( int level = 2; level <= max_levels ; level *= 2)
        {

            if (have_data)
            {
                bool reciever = ((rank % level) == 0) ? true : false;
                if ( reciever )
                {
                    uint64_t send_rank = rank + (level / 2);
                    if (send_rank >= (uint64_t) mpi_config->particle_flow_world_size) {
                        // printf("LEVEL %d: Rank %d waiting til next level\n", level, rank, send_rank);
                        continue;
                    }

                
                    uint64_t send_count;
                    uint64_t    *recv_indexes    = all_interp_node_indexes     + local_nodes_size;
                    flow_aos<T> *recv_flow_terms = all_interp_node_flow_fields + local_nodes_size;

                    MPI_Recv (&send_count,     1,          MPI_UINT64_T,                   send_rank, level, mpi_config->particle_flow_world, MPI_STATUS_IGNORE);
                    // printf("LEVEL %d: Rank %d recv from %d size %d local size %d\n", level, rank, send_rank, send_count, local_nodes_size);
                    resize_nodes_arrays(local_nodes_size + send_count); // RESIZE within comms loop

                    MPI_Recv (recv_indexes,    send_count, MPI_UINT64_T,                   send_rank, level, mpi_config->particle_flow_world, MPI_STATUS_IGNORE);
                    MPI_Recv (recv_flow_terms, send_count, mpi_config->MPI_FLOW_STRUCTURE, send_rank, level, mpi_config->particle_flow_world, MPI_STATUS_IGNORE);

                    for (uint64_t i = 0; i < send_count; i++)
                    {
                        if ( node_to_position_map.contains(recv_indexes[i]) ) // Aggregate terms
                        {
                            all_interp_node_flow_fields[node_to_position_map[recv_indexes[i]]].vel      += recv_flow_terms[i].vel;
                            all_interp_node_flow_fields[node_to_position_map[recv_indexes[i]]].temp     += recv_flow_terms[i].temp;
                            all_interp_node_flow_fields[node_to_position_map[recv_indexes[i]]].pressure += recv_flow_terms[i].pressure;

                        }
                        else // Create new entry
                        { 
                            resize_nodes_arrays(local_nodes_size + 1);
                            all_interp_node_indexes[local_nodes_size]     = recv_indexes[i];
                            all_interp_node_flow_fields[local_nodes_size] = recv_flow_terms[i];
                    
                            node_to_position_map[recv_indexes[i]] = local_nodes_size;
                            local_nodes_size++;
                        }
                    }
                }
                else
                {
                    uint64_t recv_rank  = rank - (level / 2);
                    // printf("LEVEL %d: Rank %d send to %d size %d\n", level, rank, recv_rank, local_nodes_size);

                    MPI_Ssend (&local_nodes_size,           1,                MPI_UINT64_T,                   recv_rank, level, mpi_config->particle_flow_world);
                    MPI_Ssend (all_interp_node_indexes,     local_nodes_size, MPI_UINT64_T,                   recv_rank, level, mpi_config->particle_flow_world);
                    MPI_Ssend (all_interp_node_flow_fields, local_nodes_size, mpi_config->MPI_FLOW_STRUCTURE, recv_rank, level, mpi_config->particle_flow_world);
                    
                    have_data = false;
                }
                // if (!have_data) printf("LEVEL %d: Rank %d NO DATA \n", level, rank);
            }
        }

        // int max_jumps = 1;
        // while (max_jumps < mpi_config->particle_flow_world_size)  max_jumps *= 2;
        // max_jumps /= 2;

        // MPI_Request send_requests[3], recv_requests[2];
        // for ( int jump = 1; jump <= max_jumps ; jump *= 2)
        // {
        //     int send_rank = (rank + jump)                                        % mpi_config->particle_flow_world_size;
        //     int recv_rank = (rank - jump + mpi_config->particle_flow_world_size) % mpi_config->particle_flow_world_size;

        //     // printf("Jump %d: Rank %d: Sending to %d, recieving from %d\n", jump, rank, send_rank, recv_rank);

        //     uint64_t foreign_nodes_size;
        //     uint64_t    *recv_indexes    = all_interp_node_indexes     + local_nodes_size;
        //     flow_aos<T> *recv_flow_terms = all_interp_node_flow_fields + local_nodes_size;

        //     MPI_Isend (&local_nodes_size,           1,                MPI_UINT64_T,                   send_rank, jump, mpi_config->particle_flow_world, &send_requests[0]);
        //     MPI_Isend (all_interp_node_indexes,     local_nodes_size, MPI_UINT64_T,                   send_rank, jump, mpi_config->particle_flow_world, &send_requests[1]);
        //     MPI_Isend (all_interp_node_flow_fields, local_nodes_size, mpi_config->MPI_FLOW_STRUCTURE, send_rank, jump, mpi_config->particle_flow_world, &send_requests[2]);


        //     MPI_Irecv (&foreign_nodes_size, 1, MPI_UINT64_T, recv_rank, jump, mpi_config->particle_flow_world, &recv_requests[0]);
        //     MPI_Wait  (&recv_requests[0], MPI_STATUS_IGNORE);
        //     resize_nodes_arrays(local_nodes_size + foreign_nodes_size);

        //     MPI_Irecv (recv_indexes,    foreign_nodes_size, MPI_UINT64_T,                   recv_rank, jump, mpi_config->particle_flow_world, &recv_requests[0]);
        //     MPI_Irecv (recv_flow_terms, foreign_nodes_size, mpi_config->MPI_FLOW_STRUCTURE, recv_rank, jump, mpi_config->particle_flow_world, &recv_requests[1]);
            
        //     MPI_Waitall(2, recv_requests, MPI_STATUSES_IGNORE);
        //     MPI_Waitall(3, send_requests, MPI_STATUSES_IGNORE);

        //     for (uint64_t i = 0; i < foreign_nodes_size; i++)
        //     {
        //         if ( node_to_position_map.contains(recv_indexes[i]) ) // Aggregate terms
        //         {
        //             all_interp_node_flow_fields[node_to_position_map[recv_indexes[i]]].vel      += recv_flow_terms[i].vel;
        //             all_interp_node_flow_fields[node_to_position_map[recv_indexes[i]]].temp     += recv_flow_terms[i].temp;
        //             all_interp_node_flow_fields[node_to_position_map[recv_indexes[i]]].pressure += recv_flow_terms[i].pressure;

        //         }
        //         else // Create new entry
        //         { 
        //             all_interp_node_indexes[local_nodes_size]     = recv_indexes[i];
        //             all_interp_node_flow_fields[local_nodes_size] = recv_flow_terms[i];

        //             node_to_position_map[recv_indexes[i]] = local_nodes_size;
        //             local_nodes_size++;
        //         }
        //     }
        // }
        
        time_stats45 += MPI_Wtime();
        time_stats5 -= MPI_Wtime();
        MPI_Request request;

        MPI_Bcast (&local_nodes_size,           1,                MPI_UINT64_T,                   0, mpi_config->particle_flow_world);
        MPI_Bcast (all_interp_node_indexes,     local_nodes_size, MPI_UINT64_T,                   0, mpi_config->particle_flow_world);

        MPI_Ibcast (all_interp_node_flow_fields, local_nodes_size, mpi_config->MPI_FLOW_STRUCTURE, 0, mpi_config->particle_flow_world, &request);

        for (uint64_t i = 0; i < local_nodes_size; i++)
            node_to_position_map[all_interp_node_indexes[i]] = i;

        particle_nodes_set.clear();
        MPI_Wait(&request, MPI_STATUS_IGNORE);
        // if (mpi_config->particle_flow_rank == 0)  printf("INTERP_NODE Time %d, nodes_fields_updated %d total_nodes %d\n", time-1, nodal_flow_field_map.size(), total_node_count);

        time_stats5 += MPI_Wtime();
        time_stats0 -= MPI_Wtime();
        time_stats0 += MPI_Wtime();
        

        if ( time == 1500 )
        {
            if (mpi_config->rank == 0)
            {
                printf("TIME1: %f\n", time_stats1);
                printf("TIME3: %f\n", time_stats3);
                printf("TIME4: %f\n", time_stats4);
                printf("TIME45: %f\n",time_stats45);
                printf("TIME5: %f\n", time_stats5);
                printf("TIME0: %f\n", time_stats0);
                printf("NODE AVG: %f\n", node_avg / 1500.);
            }
        }
        performance_logger.my_papi_stop(performance_logger.interpolation_kernel_event_counts, &performance_logger.interpolation_time);
    }

    template<class T> 
    void ParticleSolver<T>::timestep()
    {
        static int count = 0;
        const int  comms_timestep = 1;

        if (PARTICLE_SOLVER_DEBUG)  printf("Start particle timestep\n");
        if ( (count % 100) == 0 )
        {
            uint64_t particles_in_simulation = particles.size();
            uint64_t total_particles_in_simulation;
            MPI_Reduce(&particles_in_simulation, &total_particles_in_simulation, 1, MPI_UINT64_T, MPI_SUM, 0, mpi_config->particle_flow_world);
            if ( mpi_config->particle_flow_rank == 0 )
                cout << "\tTimestep " << count << ". Total particles in simulation " << total_particles_in_simulation << endl;
        }

        particle_release();
        if (mpi_config->world_size != 1 && (count % comms_timestep) == 0)
        {
            update_flow_field(count > 0);
            interpolate_nodal_data(); 
        }
        else if (mpi_config->world_size == 1)
        {
            interpolate_nodal_data(); 
        }
        solve_spray_equations();
        update_particle_positions();

        logger.avg_particles += (double)particles.size() / (double)num_timesteps;

        count++;

        if (PARTICLE_SOLVER_DEBUG)  printf("Stop particle timestep\n");
    }
}   // namespace minicombust::particles 