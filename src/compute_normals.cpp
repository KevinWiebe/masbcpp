// Copyright (c) 2015
// Ravi Peters -- r.y.peters@tudelft.nl
// All rights reserved
// This file is part of masbcpp.
//
// masbcpp is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// masbcpp is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with masbcpp.  If not, see <http://www.gnu.org/licenses/>.

// #define VERBOSEPRINT 1;
// #define WITH_OPENMP 1;

#include <iostream>
#include <fstream>
#include <string>

// OpenMP
#ifdef WITH_OPENMP
    #ifdef CLANG_OMP
        #include <libiomp/omp.h>
    #else
        #include <omp.h>
    #endif
#endif

// Vrui
#include <vrui/Geometry/ComponentArray.h>
#include <vrui/Math/Math.h>
#include <vrui/Misc/Timer.h>
#include <vrui/Geometry/PCACalculator.h>
// kdtree2
#include <kdtree2/kdtree2.hpp>
// cnpy
#include <cnpy/cnpy.h>
// tclap
#include <tclap/CmdLine.h>

// typedefs
#include "types.h"

Vector estimate_normal(Point &p, kdtree2::KDTree* kd_tree, int k)
{
    kdtree2::KDTreeResultVector result;
    kd_tree->n_nearest(p,k+1,result);

    Geometry::PCACalculator<3> PCACalc;
    for(int i=0; i<k+1; i++)
        PCACalc.accumulatePoint( kd_tree->the_data[ result[i].idx ] );

    double eigen_values[3];
    PCACalc.calcCovariance();
    PCACalc.calcEigenvalues(eigen_values);
    return PCACalc.calcEigenvector(eigen_values[2]);
}

VectorList estimate_normals(PointList &points, kdtree2::KDTree* kd_tree, int k)
{
    VectorList normals(points.size());

    #pragma omp parallel for
    for( uint i=0; i<points.size(); i++ )
        normals[i] = estimate_normal( points[i], kd_tree, k );

    return normals;
}


int main(int argc, char **argv)
{
    // parse command line arguments
    try {
        TCLAP::CmdLine cmd("Estimates normals using PCA", ' ', "0.1");

        TCLAP::UnlabeledValueArg<std::string> inputArg( "input", "path to directory with inside it a 'coords.npy' file", true, "", "input dir", cmd);
        TCLAP::UnlabeledValueArg<std::string> outputArg( "ouput", "path to output directory", true, "", "output dir", cmd);

        TCLAP::ValueArg<int> kArg("k","kneighbours","number of nearest neighbours to use for PCA",false,10,"int", cmd);
        
        cmd.parse(argc,argv);
        
        int k = kArg.getValue();

        std::string input_coords_path = inputArg.getValue()+"/coords.npy";
        std::string output_path = outputArg.getValue()+"/normals.npy";
        // check for proper in-output arguments
        {
            std::ifstream infile(input_coords_path.c_str());
            if(!infile)
                throw TCLAP::ArgParseException("invalid filepath", inputArg.getValue());
        }
        {
            std::ofstream outfile(output_path.c_str());    
            if(!outfile)
                throw TCLAP::ArgParseException("invalid filepath", outputArg.getValue());
        }
        
        std::cout << "Parameters: k="<<k<<"\n";

        cnpy::NpyArray coords_npy = cnpy::npy_load( input_coords_path.c_str() );
        float* coords_carray = reinterpret_cast<float*>(coords_npy.data);

        uint num_points = coords_npy.shape[0];
        uint dim = coords_npy.shape[1];
        PointList coords(num_points);
        for ( int i=0; i<num_points; i++) coords[i] = Point(&coords_carray[i*3]);
        coords_npy.destruct();

        
        Misc::Timer t0;
        kdtree2::KDTree* kd_tree;
        kd_tree = new kdtree2::KDTree(coords,true);
        kd_tree->sort_results = false;
        t0.elapse();
        std::cout<<"Constructed kd-tree in "<<t0.getTime()*1000.0<<" ms"<<std::endl;

        // omp_set_num_threads(1);

        {
            Scalar* normals_carray = new Scalar[num_points*3];
            Misc::Timer t1;
            VectorList normals = estimate_normals(coords, kd_tree, k);
            t1.elapse();
            std::cout<<"Done estimating normals, took "<<t1.getTime()*1000.0<<" ms"<<std::endl;
        
            
            for (int i=0; i<normals.size(); i++)
                for (int j=0; j<3; j++)
                    normals_carray[i*3+j] = normals[i][j];
        
            const unsigned int c_size = normals.size();
            const unsigned int shape[] = {c_size,3};
            cnpy::npy_save(output_path.c_str(), normals_carray, shape, 2, "w");
        }

    } catch (TCLAP::ArgException &e) { std::cerr << "Error: " << e.error() << " for " << e.argId() << std::endl; }

    return 0;
}