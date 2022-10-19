# miniCOMBUST

## Dependencies
- Catch++ Unit Testing (Header-only)

## Build

Tested with GCC, Cray and Intel compilers, Intel is most tested compiler at the moment.

Without PAPI:
```bash
make clean all
```

With PAPI:
```bash
PAPI=1 make clean all
```

## Run 
```bash
./bin/minicombust # emits 10 particles per timestep by default
```

```bash
./bin/minicombust NUM_PARTICLES_PER_TIMESTEP
```

## Run tests 
```bash
./bin/minicombust_tests
```

## Output

Output vtk files for the mesh and particles are written to `out/`

## Get roofline CMD (PAPI Build Required)

Generates command for roofline repo https://github.com/UoB-HPC/roofline. Plots each MiniCOMBUST kernel. 

```python
python analysis/get_roofline_cmd.py CASCADE_LAKE 1-core out/performance.csv
## OR
python analysis/get_roofline_cmd.py TX2 2-socket 64
```

## Scaling experiments

### Weak particle scaling experiment. 

Runs MiniCOMBUST with a fixed size mesh on a fixed number of processes, while varying the number of particles (doubling to upper bound). 

For different systems, provide a template job in `jobs/templates/`. We provide an example for Isambard, ThunderX2. Then edit the `TEMPLATE` env variable.

```bash
./analysis/analysis particle_weak_scaling.sh LOW_PARTICLE_BOUND HIGH_PARTICLE_BOUND CELLS_MODIFIER NODES PPN
```

### Weak mesh scaling experiment. 

Runs MiniCOMBUST with a fixed number of particles on a fixed number of processes, while varying the size of the mesh (doubling to upper bound). 

For different systems, provide a template job in `jobs/templates/`. We provide an example for Isambard, ThunderX2. Then edit the `TEMPLATE` env variable.

```bash
./analysis/analysis mesh_weak_scaling.sh LOW_CELL_MODIFIER_BOUND HIGH_CELL_MODIFIER_BOUND PARTICLES NODES PPN
```

## Future Features
- Primary breakup
- C API for particle side
- Tetrahedral mesh
- Benchmarks
- MPI Implementationi
- YAML config file

## References
- (Parallel load-balancing for combustion with spray for large-scale simulation)[https://www.sciencedirect.com/science/article/pii/S0021999121000826?via%3Dihub#br0160]
- (Particle breakup)
  [https://d1wqtxts1xzle7.cloudfront.net/30787458/GetTRDoc-libre.pdf?1392132662=&response-content-disposition=inline%3B+filename%3DAn_experimental_and_computational_invest.pdf&Expires=1649411271&Signature=UjaWeJ468xnrPetPPHzfeTmawxW-0i7yUb~39pAPXPzfpQ4AD2fdyijRPQ39KNj3UkIcxx4MoWysHGcaebDvLQTKvqye89ibtGhwkOxXAiZyfJZ1H2nPZGIWOdCqe6X15D4KGOGLYglb0o0SeuccQsh6p~BNFNh1WAiEoqsYOf6aQhc2rl0hNO8s5lqYBodlGFjVFEaiNqkZu8t3U3AQ0TXu~kk10TmB1asHH69oyR5K9cTNcnarHlKAcnzd6BbIMJJFqE2nPYHTDHtOrELC-eRAhWqbJxyFNKfAMVUEldIn9cV5GH~x7YYfKWzsBcI2tMwm1CuMsu9GHHptueA6jw__&Key-Pair-Id=APKAJLOHF5GGSLRBV4ZA#page=166]
- (Particle evaporation)[https://arc.aiaa.org/doi/10.2514/3.8264]
- (Particle breakup)[https://www.sciencedirect.com/science/article/pii/S0301932203001113?via=ihub]
- (Particle breakup parceling)[https://www.sciencedirect.com/science/article/pii/S1540748908002678#bb0085]
