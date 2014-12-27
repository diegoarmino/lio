/* includes */
#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cassert>

#include "common.h"
#include "init.h"
#include "partition.h"

using namespace std;
using namespace G2G;

/************************************************************
 * Construct partition
 ************************************************************/

//Sorting the cubes in increasing order of size in bytes in GPU.
template <typename T>
bool comparison_by_size(const T & a, const T & b) {
  return a.size_in_gpu() < b.size_in_gpu();
}

template <typename T>
void sortBySize(std::vector<T> & input) {
  sort(input.begin(), input.end(), comparison_by_size<T>);
}

void load_pools(const vector<int> & elements, const vector< vector<int> > & work, vector< int > & pool_sizes) {
  pool_sizes.clear();
  for(int i = 0; i < work.size(); i++) {
    int largest_pool = 0;
    for(int j = 0; j < work[i].size(); j++) {
      largest_pool = max(largest_pool, elements[work[i][j]]);
    }
    pool_sizes.push_back(largest_pool);
  }
}

template <typename T>
long long total_costs(const vector<T*> & elements)
{
    long long res = 0;
    for(int i = 0; i < elements.size(); i++)
        res += elements[i]->cost();
    return res;
}

int split_bins(const vector< pair<long long, int> > & costs, vector< vector<int> > & workloads, long long capacity)
{
  // Bin Packing heuristic
  workloads.clear();
  for(int i = 0; i < costs.size(); i++) {
    int next_bin = -1;
    for(int j = 0; j < workloads.size(); j++) {
      long long slack = capacity;
      for(int k = 0; k < workloads[j].size(); k++){
        slack -= costs[workloads[j][k]].first;
      }
      if (slack >= costs[i].first && next_bin == -1) {
        next_bin = j;
        break;
      }
    }
    if(next_bin == -1) {
      if (capacity < costs[i].second) {
        return INT_MAX;
      }
      next_bin = workloads.size();
      workloads.push_back(vector<int>());
    }
    workloads[next_bin].push_back(i);
  }
  return workloads.size();
}

void Partition::compute_work_partition()
{
  vector< pair<long long, int> > costs;
  for(int i = 0; i < cubes.size(); i++)
    if(!cubes[i]->is_big_group(inner_threads))
      costs.push_back(make_pair(cubes[i]->cost(), i));

  const int ncubes = cubes.size();
  for(int i = 0; i < spheres.size(); i++)
    if(!spheres[i]->is_big_group(inner_threads))
      costs.push_back(make_pair(spheres[i]->cost(), ncubes+i));

  if(costs.size() == 0) return;

  sort(costs.begin(), costs.end());
  reverse(costs.begin(), costs.end());

  long long min_cost = costs.front().second - 1,
            max_cost = total_costs(cubes) + total_costs(spheres) + 1;

  while(max_cost - min_cost > 1) {
    long long candidate = min_cost + (max_cost - min_cost)/2;

    vector< vector<int> > workloads;
    int bins = split_bins(costs, workloads, candidate);
    if(bins <= outer_threads) {
      max_cost = candidate;
    } else {
      min_cost = candidate;
    }
  }

  split_bins(costs, work, max_cost);
  for(int i = 0; i < work.size(); i++)
      sort(work[i].begin(), work[i].end());

  double maxp = 0, minp = total_costs(cubes)+total_costs(spheres)+1;
  for(int i = 0; i < work.size(); i++) {
    long long total = 0;
    for(int j = 0; j < work[i].size(); j++) {
      long long c = costs[work[i][j]].first;
      work[i][j] = costs[work[i][j]].second;
      total += c;
    }
    if(minp > total) minp = total;
    if(maxp < total) maxp = total;
    printf("Particion %d: %lld\n", i, total);
  }
  printf("Relacion max / min = %lf\n", maxp / minp);
}

int getintenv(const char * str, int default_value) {
  char * v = getenv(str);
  if (v == NULL) return default_value;
  int ret = strtol(v, NULL, 10);
  return ret;
}

void diagnostic(int inner, int outer) {
    printf("--> Thread OMP: %d\n", omp_get_max_threads());
    printf("--> Thread internos: %d\n", inner);
    printf("--> Thread externos: %d\n", outer);
    printf("--> Correccion de cubos chicos: %d\n", MINCOST);
    printf("--> Threshold en threads para considerar grande: %d\n", THRESHOLD);
}

template <class T>
bool is_big_group(const T& points) {
    return (points.size() > getintenv("LIO_SPLIT_POINTS", 600));
}

template <class T>
struct sorter {
  bool operator() (const T* l, const T* r) { return (l->cost() < r->cost());}
};


/* methods */
void Partition::regenerate(void)
{
    Timer tweights;
    // Determina el exponente minimo para cada tipo de atomo.
    // uno por elemento de la tabla periodica.
    vector<double> min_exps(120, numeric_limits<double>::max());
    for (uint i = 0; i < fortran_vars.m; i++)
    {
        uint contractions = fortran_vars.contractions(i);
        uint nuc = fortran_vars.nucleii(i) - 1;
        uint nuc_type = fortran_vars.atom_types(nuc);
        for (uint j = 0; j < contractions; j++)
        {
            min_exps[nuc_type] = min(min_exps[nuc_type], fortran_vars.a_values(i, j));
        }
    }

    // Un exponente y un coeficiente por funcion.
    vector<double> min_exps_func(fortran_vars.m, numeric_limits<double>::max());
    vector<double> min_coeff_func(fortran_vars.m);
    for (uint i = 0; i < fortran_vars.m; i++)
    {
        uint contractions = fortran_vars.contractions(i);
        for (uint j = 0; j < contractions; j++)
        {
            if (fortran_vars.a_values(i, j) < min_exps_func[i])
            {
                min_exps_func[i] = fortran_vars.a_values(i, j);
                min_coeff_func[i] = fortran_vars.c_values(i, j);
            }
        }
    }

    // Encontrando el prisma conteniendo el sistema.
    double3 x0 = make_double3(0,0,0);
    double3 x1 = make_double3(0,0,0);
    for (uint atom = 0; atom < fortran_vars.atoms; atom++)
    {
        double3 atom_position(fortran_vars.atom_positions(atom));
        uint atom_type = fortran_vars.atom_types(atom);
        // TODO: max_radios esta al doble de lo que deberia porque el criterio no esta bien aplicado.
        double max_radius = 2 * sqrt(max_function_exponent / min_exps[atom_type]);
        _DBG(cout << "tipo: " << atom_type << " " << min_exps[atom_type] << " radio: " << max_radius << endl);
        double3 tuple_max_radius = make_double3(max_radius, max_radius, max_radius);
        if (atom == 0)
        {
            x0 = atom_position - tuple_max_radius;
            x1 = atom_position + tuple_max_radius;
        }
        else
        {
            x0.x = min(x0.x, atom_position.x - max_radius);
            x0.y = min(x0.y, atom_position.y - max_radius);
            x0.z = min(x0.z, atom_position.z - max_radius);

            x1.x = max(x1.x, atom_position.x + max_radius);
            x1.y = max(x1.y, atom_position.y + max_radius);
            x1.z = max(x1.z, atom_position.z + max_radius);
        }
    }

    // El prisma tiene vertices (x,y), con x0 el vertice inferior, izquierdo y mas lejano
    // y x1 el vertice superior, derecho y mas cercano.

    // Generamos la particion en cubos.
    uint3 prism_size = ceil_uint3((x1 - x0) / little_cube_size);

    typedef vector<Point> Group;
    vector<vector<vector<Group> > >
      prism(prism_size.x, vector<vector<Group> >(prism_size.y, vector<Group>(prism_size.z)));

    // Inicializamos las esferas.
    vector<Group> sphere_points;
    vector<double> sphere_radius_array;
    if (sphere_radius > 0)
    {
        sphere_radius_array.resize(fortran_vars.atoms);
        sphere_points.resize(fortran_vars.atoms);
        for (uint atom = 0; atom < fortran_vars.atoms; atom++)
        {
            uint atom_shells = fortran_vars.shells(atom);
            uint included_shells = (uint)ceil(sphere_radius * atom_shells);
            double radius;
            if (included_shells == 0)
            {
                radius = 0;
            }
            else
            {
                double x = cos((M_PI / (atom_shells + 1)) * (atom_shells - included_shells + 1));
                double rm = fortran_vars.rm(atom);
                radius = rm * (1.0 + x) / (1.0 - x);
            }
            _DBG(cout << "esfera incluye " << included_shells << " capas de " << atom_shells << " (radio: " << radius << ")" << endl);
            sphere_radius_array[atom] = radius;
        }
    }

    // Precomputamos las distancias entre atomos.
    for (uint i = 0; i < fortran_vars.atoms; i++)
    {
        const double3& atom_i_position(fortran_vars.atom_positions(i));
        double nearest_neighbor_dist = numeric_limits<double>::max();

        double sphere_i_radius = (sphere_radius > 0 ? sphere_radius_array[i] : 0);

        for (uint j = 0; j < fortran_vars.atoms; j++)
        {
            const double3& atom_j_position(fortran_vars.atom_positions(j));
            double dist = length(atom_i_position - atom_j_position);
            fortran_vars.atom_atom_dists(i, j) = dist;
            if (i != j)
                nearest_neighbor_dist = min(nearest_neighbor_dist, dist);
        }
        fortran_vars.nearest_neighbor_dists(i) = nearest_neighbor_dist;
    }

    // Computamos los puntos y los asignamos a los cubos y esferas.
    uint puntos_totales = 0;
    uint puntos_finales = 0;
    uint funciones_finales = 0;
    uint costo = 0;

    // Limpiamos las colecciones de las esferas y cubos que tengamos guardadas.
    this->clear();

    // Computa las posiciones de los puntos (y los guarda).
    for (uint atom = 0; atom < fortran_vars.atoms; atom++)
    {
        uint atom_shells = fortran_vars.shells(atom);
        const double3& atom_position(fortran_vars.atom_positions(atom));

        double t0 = M_PI / (atom_shells + 1);
        double rm = fortran_vars.rm(atom);

        puntos_totales += (uint) fortran_vars.grid_size * atom_shells;
        for (uint shell = 0; shell < atom_shells; shell++)
        {
            double t1 = t0 * (shell + 1);
            double x = cos(t1);
            double w = t0 * abs(sin(t1));
            double r1 = rm * (1.0 + x) / (1.0 - x);
            double wrad = w * (r1 * r1) * rm * 2.0 / ((1.0 - x) * (1.0 - x));

            for (uint point = 0; point < (uint)fortran_vars.grid_size; point++)
            {
                double3 rel_point_position = make_double3(fortran_vars.e(point,0), fortran_vars.e(point,1), fortran_vars.e(point,2));
                double3 point_position = atom_position + rel_point_position * r1;
                bool inside_prism = ((x0.x <= point_position.x && point_position.x <= x1.x) &&
                                     (x0.y <= point_position.y && point_position.y <= x1.y) &&
                                     (x0.z <= point_position.z && point_position.z <= x1.z));
                if (inside_prism)
                {
                    double point_weight = wrad * fortran_vars.wang(point); // integration weight
                    Point point_object(atom, shell, point, point_position, point_weight);
                    uint included_shells = (uint)ceil(sphere_radius * atom_shells);

                    // Si esta capa esta muy lejos del nucleo, la modelamos como esfera, sino como cubo.
                    if (shell >= (atom_shells - included_shells))
                    {
                        // Asignamos este punto a la esfera de este atomo.
                        sphere_points[atom].push_back(point_object);
                    }
                    else
                    {
                        // Insertamos este punto en el cubo correspondiente.
                        uint3 cube_coord = floor_uint3((point_position - x0) / little_cube_size);
                        if (cube_coord.x >= prism_size.x || cube_coord.y >= prism_size.y || cube_coord.z >= prism_size.z)
                            throw std::runtime_error("Se accedio a un cubo invalido");
                        prism[cube_coord.x][cube_coord.y][cube_coord.z].push_back(point_object);
                    }
                }
            }
        }
    }

    // La grilla computada ahora tiene |puntos_totales| puntos, y |fortran_vars.m| funciones.
    uint nco_m = 0;
    uint m_m = 0;

    puntos_finales = 0;
    // Completamos los parametros de los cubos y los agregamos a la particion.
    for (uint i = 0; i < prism_size.x; i++)
    {
        for (uint j = 0; j < prism_size.y; j++)
        {
            for (uint k = 0; k < prism_size.z; k++)
            {
                Group& points_ijk = prism[i][j][k];

                double3 cube_coord_abs = x0 + make_uint3(i,j,k) * little_cube_size;
                PointGroup<base_scalar_type>* cube_funcs;
                #if GPU_KERNELS
                if(is_big_group(points_ijk))
                  cube_funcs = new PointGroupGPU<base_scalar_type>();
                else
                  cube_funcs = new PointGroupCPU<base_scalar_type>();
                #else
                cube_funcs = new PointGroupCPU<base_scalar_type>();
                #endif

                for(int point = 0; point < prism[i][j][k].size(); point++)
                  cube_funcs->add_point(prism[i][j][k][point]);

                cube_funcs->assign_functions_as_cube(cube_coord_abs, min_exps_func, min_coeff_func);

                if ((cube_funcs->total_functions_simple() == 0) || (cube_funcs->number_of_points < min_points_per_cube)) {
                  // Este cubo no tiene funciones o no tiene suficientes puntos.
                  delete cube_funcs;
                  continue;
                }
                PointGroup<base_scalar_type>* cube;

                #if GPU_KERNELS
                if(is_big_group(points_ijk))
                  cube = new PointGroupGPU<base_scalar_type>(*
                      (static_cast<PointGroupGPU<base_scalar_type> *>(cube_funcs)));
                else
                  cube = new PointGroupCPU<base_scalar_type>(*
                      (static_cast<PointGroupCPU<base_scalar_type> *>(cube_funcs)));
                #else
                cube = new PointGroupCPU<base_scalar_type>(*
                    (static_cast<PointGroupCPU<base_scalar_type> *>(cube_funcs)));
                #endif

                delete cube_funcs;

                tweights.start();
                cube->compute_weights();
                tweights.pause();

                if (cube->number_of_points < min_points_per_cube)
                {
                    cout << "CUBE: not enough points" << endl;
                    delete cube;
                    continue;
                }
                cubes.push_back(cube);

                puntos_finales += cube->number_of_points;
                funciones_finales += cube->number_of_points * cube->total_functions();
                costo += cube->number_of_points * (cube->total_functions() * cube->total_functions());
                nco_m += cube->total_functions() * fortran_vars.nco;
                m_m += cube->total_functions() * cube->total_functions();
            }
        }
    }

    // Si esta habilitada la particion en esferas, entonces clasificamos y las agregamos a la particion tambien.
    if (sphere_radius > 0)
    {
        for (uint i = 0; i < fortran_vars.atoms; i++)
        {
            Group& sphere_i = sphere_points[i];
            assert(sphere_i.size() != 0);

            PointGroup<base_scalar_type>* sphere_funcs;
            #if GPU_KERNELS
            if(is_big_group(sphere_i))
              sphere_funcs = new PointGroupGPU<base_scalar_type>();
            else
              sphere_funcs = new PointGroupCPU<base_scalar_type>();
            #else
            sphere_funcs = new PointGroupCPU<base_scalar_type>();
            #endif
            for(int point = 0; point < sphere_i.size(); point++)
              sphere_funcs->add_point(sphere_i[point]);

            sphere_funcs->assign_functions_as_sphere(i, sphere_radius_array[i], min_exps_func, min_coeff_func);
            assert(sphere_funcs->total_functions_simple() != 0);
            if (sphere_funcs->number_of_points < min_points_per_cube)
            {
                cout << "not enough points" << endl;
                delete sphere_funcs;
                continue;
            }

            PointGroup<base_scalar_type>* sphere;
            #if GPU_KERNELS
            if(is_big_group(sphere_i)) {
              sphere = new PointGroupGPU<base_scalar_type>(*
                  (static_cast<PointGroupGPU<base_scalar_type> *>(sphere_funcs)));
            }
            else {
              sphere = new PointGroupCPU<base_scalar_type>(*
                  (static_cast<PointGroupCPU<base_scalar_type> *>(sphere_funcs)));
            }
            #else
            sphere = new PointGroupCPU<base_scalar_type>(*
                (static_cast<PointGroupCPU<base_scalar_type> *>(sphere_funcs)));
            #endif
            delete sphere_funcs;

            assert(sphere->number_of_points != 0);
            tweights.start();
            sphere->compute_weights();
            tweights.pause();
            if (sphere->number_of_points < min_points_per_cube)
            {
                cout << "not enough points" << endl;
                delete sphere;
                continue;
            }
            assert(sphere->number_of_points != 0);
            spheres.push_back(sphere);

            puntos_finales += sphere->number_of_points;
            funciones_finales += sphere->number_of_points * sphere->total_functions();
            costo += sphere->number_of_points * (sphere->total_functions() * sphere->total_functions());
            nco_m += sphere->total_functions() * fortran_vars.nco;
            m_m += sphere->total_functions() * sphere->total_functions();
        }
    }

    // TODO fix these sorts now that spheres and cubes are pointers
    //sort(spheres.begin(), spheres.end(), sorter());
    //sort(cubes.begin(), cubes.end(), sorter());

    //Initialize the global memory pool for CUDA, with the default safety factor
    //If it is CPU, then this doesn't matter
    GlobalMemoryPool::init(G2G::free_global_memory);

    inner_threads = outer_threads = omp_get_max_threads();

    G2G::MINCOST = getintenv("LIO_MINCOST_OFFSET", 250000);
    G2G::THRESHOLD = getintenv("LIO_SPLIT_THRESHOLD", 80);

    cout << "Weights: " << tweights << endl;

    #ifdef OUTPUT_COSTS
    for(int i = 0; i < cubes.size(); i++) {
      printf("CUBE: "); cubes[i]->output_cost(); printf("\n");
    }
    for(int i = 0; i < spheres.size(); i++) {
      printf("SPHERE: "); spheres[i]->output_cost(); printf("\n");
    }
    #endif

    for(int i = 0; i < cubes.size(); i++) {
      cubes[i]->compute_indexes();
    }
    for(int i = 0; i < spheres.size(); i++) {
      spheres[i]->compute_indexes();
    }

    compute_work_partition();
    int gpu_threads = 0;
#if GPU_KERNELS
    cudaGetDeviceCount(&gpu_threads);
#endif

    timeforgroup.resize(cubes.size() + spheres.size());
    next.resize(outer_threads+gpu_threads);

    fort_forces_ms.resize(outer_threads+gpu_threads);
    rmm_outputs.resize(outer_threads+gpu_threads);

    for(int i = 0; i < outer_threads+gpu_threads; i++) {
      fort_forces_ms[i].resize(fortran_vars.max_atoms, 3);
      rmm_outputs[i].resize(fortran_vars.rmm_output.width, fortran_vars.rmm_output.height);
    }

    int current_gpu = 0;
    for(int i = 0; i<gpu_threads; i++)
      work.push_back(vector<int>());

    for(int i = 0; i < cubes.size(); i++)
      if(cubes[i]->is_big_group(inner_threads)) {
        work[outer_threads+current_gpu].push_back(i);
        current_gpu = (current_gpu + 1) % gpu_threads;
      }

    for(int i = 0; i < spheres.size(); i++)
      if(spheres[i]->is_big_group(inner_threads)) {
        work[outer_threads+current_gpu].push_back(i+cubes.size());
        current_gpu = (current_gpu + 1) % gpu_threads;
      }

    diagnostic(inner_threads, outer_threads);
}
