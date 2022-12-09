#include <stdio.h>
#include <ctime>
#include <inttypes.h>

#include "examples/mesh_examples.hpp"
#include "examples/particle_examples.hpp"
#include "geometry/Mesh.hpp"
#include "utils/utils.hpp"


#include "particles/ParticleSolver.inl"
#include "flow/FlowSolver.inl"


using namespace minicombust::flow;
using namespace minicombust::particles;
using namespace minicombust::visit;

int main (int argc, char ** argv)
{
    // MPI Initialisation 
    MPI_Init(NULL, NULL);
    MPI_Config mpi_config;
    mpi_config.world = MPI_COMM_WORLD;
    MPI_Comm_rank(mpi_config.world,  &mpi_config.rank);
    MPI_Comm_size(mpi_config.world,  &mpi_config.world_size);
    mpi_config.solver_type = (mpi_config.world_size == 1 || mpi_config.rank < mpi_config.world_size - 1); // 1 for particle, 0 for flow
    MPI_Comm_split(mpi_config.world, mpi_config.solver_type, mpi_config.rank, &mpi_config.particle_flow_world);
    MPI_Comm_rank(mpi_config.particle_flow_world,  &mpi_config.particle_flow_rank);
    MPI_Comm_size(mpi_config.particle_flow_world,  &mpi_config.particle_flow_world_size);

    MPI_Type_contiguous(sizeof(flow_aos<double>)/sizeof(double),     MPI_DOUBLE, &mpi_config.MPI_FLOW_STRUCTURE);
    MPI_Type_contiguous(sizeof(particle_aos<double>)/sizeof(double), MPI_DOUBLE, &mpi_config.MPI_PARTICLE_STRUCTURE);
    MPI_Type_commit(&mpi_config.MPI_FLOW_STRUCTURE);
    MPI_Type_commit(&mpi_config.MPI_PARTICLE_STRUCTURE);

    MPI_Op_create(&sum_particle_aos<double>, 1, &mpi_config.MPI_PARTICLE_OPERATION);
    
    if (mpi_config.rank == 0)  
    {
        printf("Starting miniCOMBUST..\n");
        printf("MPI Configuration:\n\tFlow Ranks: %d\n\tParticle Ranks: %d\n", mpi_config.world_size - mpi_config.particle_flow_world_size, mpi_config.particle_flow_world_size);
    }

    // Run Configuration
    const uint64_t ntimesteps                   = 1500;
    const double   delta                        = 2.5e-6;
    const int64_t output_iteration              = (argc > 3) ? atoi(argv[3]) : 10;
    const uint64_t particles_per_timestep       = (argc > 1) ? atoi(argv[1]) : 10;
    
    // Mesh Configuration
    const vec<double>   box_dim                 = {0.10, 0.05, 0.05};
    const uint64_t modifier                     = (argc > 2) ? atoi(argv[2]) : 10;
    const vec<uint64_t> elements_per_dim        = {modifier*2,   modifier*1,  modifier*1};


    


    // Performance
    double setup_time = 0., program_time = 0., output_time = 0.;

    // Perform setup and benchmark cases
    MPI_Barrier(mpi_config.world); setup_time  -= MPI_Wtime(); 
    Mesh<double> *mesh                          = load_mesh(&mpi_config, box_dim, elements_per_dim);

    //Setup solvers
    ParticleSolver<double> *particle_solver = nullptr;
    FlowSolver<double>     *flow_solver     = nullptr;
    if (mpi_config.solver_type == PARTICLE)
    {
        uint64_t       local_particles_per_timestep   = particles_per_timestep / mpi_config.particle_flow_world_size;
        int            remainder_particles            = particles_per_timestep % mpi_config.particle_flow_world_size;

        const uint64_t reserve_particles_size         = 2 * (local_particles_per_timestep + 1) * ntimesteps;

        ParticleDistribution<double> *particle_dist = load_particle_distribution(local_particles_per_timestep, remainder_particles, &mpi_config, mesh);
        particle_solver = new ParticleSolver<double>(&mpi_config, ntimesteps, delta, particle_dist, mesh, reserve_particles_size); 
    }
    else
    {
        flow_solver     = new FlowSolver<double>(&mpi_config, mesh);
    }
    if (mpi_config.rank == 0)   cout << endl;
    setup_time += MPI_Wtime(); MPI_Barrier(mpi_config.world); 

    // Output mesh 
    MPI_Barrier(mpi_config.world); output_time -= MPI_Wtime(); 
    if (mpi_config.rank == 0)
    {
        VisitWriter<double> *vtk_writer = new VisitWriter<double>(mesh);
        if (output_iteration != -1) vtk_writer->write_mesh("minicombust");
         
    }
    output_time += MPI_Wtime(); MPI_Barrier(mpi_config.world); 

    // Main loop
    if (mpi_config.rank == 0)  printf("Starting simulation..\n");
    MPI_Barrier(mpi_config.world);
    program_time -= MPI_Wtime();
    for(uint64_t t = 0; t < ntimesteps; t++)
    {
        if (mpi_config.solver_type == PARTICLE) 
            particle_solver->timestep();
        else
            flow_solver->timestep();
        
        if (((int64_t)(t % output_iteration) == output_iteration - 1) && mpi_config.rank == 0)  
        {
            output_time -= MPI_Wtime();
            particle_solver->output_data(t+1);
            output_time += MPI_Wtime();
        }
    }
    program_time += MPI_Wtime();
    MPI_Barrier(mpi_config.world);
    if (mpi_config.rank == 0) printf("Done!\n\n");

    //Print logger stats and write performance counters
    if (LOGGER) 
    {
        if (mpi_config.solver_type == PARTICLE)
            particle_solver->print_logger_stats(ntimesteps, program_time);
        else
            flow_solver->performance_logger.print_counters(mpi_config.rank, mpi_config.world_size, program_time);
    }

    // Get program times
    double setup_time_avg = 0., program_time_avg = 0., output_time_avg = 0.;
    double setup_time_max = 0., program_time_max = 0., output_time_max = 0.;
    double setup_time_min = 0., program_time_min = 0., output_time_min = 0.;
    MPI_Reduce(&setup_time,    &setup_time_avg,   1, MPI_DOUBLE, MPI_SUM, 0, mpi_config.world);
    MPI_Reduce(&setup_time,    &setup_time_max,   1, MPI_DOUBLE, MPI_MAX, 0, mpi_config.world);
    MPI_Reduce(&setup_time,    &setup_time_min,   1, MPI_DOUBLE, MPI_MIN, 0, mpi_config.world);
    MPI_Reduce(&program_time,  &program_time_avg, 1, MPI_DOUBLE, MPI_SUM, 0, mpi_config.world);
    MPI_Reduce(&program_time,  &program_time_max, 1, MPI_DOUBLE, MPI_MAX, 0, mpi_config.world);
    MPI_Reduce(&program_time,  &program_time_min, 1, MPI_DOUBLE, MPI_MIN, 0, mpi_config.world);
    MPI_Reduce(&output_time,   &output_time_avg,  1, MPI_DOUBLE, MPI_SUM, 0, mpi_config.world);
    MPI_Reduce(&output_time,   &output_time_max,  1, MPI_DOUBLE, MPI_MAX, 0, mpi_config.world);
    MPI_Reduce(&output_time,   &output_time_min,  1, MPI_DOUBLE, MPI_MIN, 0, mpi_config.world);

    if (mpi_config.rank == 0)
    {
        setup_time_avg    /= (double)mpi_config.world_size;
        program_time_avg  /= (double)mpi_config.world_size;
        output_time_avg   /= (double)mpi_config.world_size;

        double precision = 5+log10(max({setup_time_max, program_time_max, output_time_max}));
        cout.precision(2);
        cout.setf(ios::fixed);
        cout << "Setup Time:    " << setw(precision) << setup_time_avg   << "s  (min " << setw(precision) << setup_time_min   << "s) " << "(max " << setw(precision) << setup_time_max    << "s)\n";
        cout << "Program Time:  " << setw(precision) << program_time_avg << "s  (min " << setw(precision) << program_time_min << "s) " << "(max " << setw(precision) << program_time_max  << "s)\n";
        cout << "Output Time:   " << setw(precision) << output_time_avg  << "s  (min " << setw(precision) << output_time_min  << "s) " << "(max " << setw(precision) << output_time_max   << "s)\n";
    }
    MPI_Finalize();

    

    return 0;
}
