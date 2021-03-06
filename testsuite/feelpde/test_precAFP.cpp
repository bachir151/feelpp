/* -*- mode: c++; coding: utf-8; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; show-trailing-whitespace: t
   -*- vim: set ft=cpp fenc=utf-8 sw=4 ts=4 sts=4 tw=80 et cin cino=N-s,c0,(0,W4,g0:

  This file is part of the Feel library

  Author(s): Christophe Prud'homme <christophe.prudhomme@feelpp.org>
       Date: 2008-02-07

  Copyright (C) 2008-2012 Universite Joseph Fourier (Grenoble I)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
   \file dist2wallsoptimized.cpp
   \author Guillaume Dolle <gdolle at unistra.fr>
   \date 2014-01-21
 */

//#define USE_BOOST_TEST 1
#if defined(USE_BOOST_TEST)
#define BOOST_TEST_MODULE precAFP
#include <testsuite/testsuite.hpp>
#endif

#include <feel/feel.hpp>
#include <feel/feelpde/preconditionerblockms.hpp>
#include <feel/feelmodels/modelproperties.hpp>
#include <feel/feeldiscr/ned1h.hpp>

#if FM_DIM == 2
#define curl_op curlx
#define curlt_op curlxt
#define curlv_op curlxv
#else
#define curl_op curl
#define curlt_op curlt
#define curlv_op curlv
#endif

using namespace Feel;


inline
po::options_description
makeOptions()
{
    po::options_description opts( "test_precAFP" );
    opts.add_options()
    ( "saveTimers", po::value<bool>()->default_value( true ), "print timers" )
    ( "myModel", po::value<std::string>()->default_value( "model.mod" ), "name of the model" )
    ;
    return opts.add( Feel::feel_options() )
        .add(Feel::backend_options("ms"));
}

inline
AboutData
makeAbout()
{
#if FM_DIM==2
    AboutData about( "precAFP2D" ,
                     "precAFP2D" ,
                     "0.1",
                     "test precAFP2D",
                     Feel::AboutData::License_GPL,
                     "Copyright (c) 2015 Feel++ Consortium" );
#else
    AboutData about( "precAFP3D" ,
                     "precAFP3D" ,
                     "0.1",
                     "test precAFP3D",
                     Feel::AboutData::License_GPL,
                     "Copyright (c) 2015 Feel++ Consortium" );
#endif

    about.addAuthor( "Vincent HUBER", "developer", "vincent.huber@cemosis.fr", "" );

    return about;
}

///     \tparam DIM         Topological dimension.
template<int DIM>
class TestPrecAFP : public Application
{
    private:
    typedef Application super;
    //! Numerical type is double
    typedef double value_type;

    //! Simplexes of order ORDER
    typedef Simplex<DIM> convex_type;
    typedef Mesh<convex_type> mesh_type;
    typedef boost::shared_ptr<mesh_type> mesh_ptrtype;

    //! Hcurl space
    typedef Nedelec<0,NedelecKind::NED1 > curl_basis_type;
    typedef FunctionSpace<mesh_type, bases<curl_basis_type>> curl_space_type;
    typedef boost::shared_ptr<curl_space_type> curl_space_ptrtype;
    typedef typename curl_space_type::element_type curl_element_type;

    //! Pch space
    typedef Lagrange<1, Scalar> lag_basis_type; 
    typedef FunctionSpace<mesh_type, bases<lag_basis_type>> lag_space_type;
    typedef boost::shared_ptr<lag_space_type> lag_space_ptrtype;
    typedef typename lag_space_type::element_type lag_element_type;

    //! Pch 0 space
    typedef Lagrange<0, Scalar, Discontinuous> lag_0_basis_type; 
    typedef FunctionSpace<mesh_type, bases<lag_0_basis_type>, Continuous> lag_0_space_type;
    typedef boost::shared_ptr<lag_0_space_type> lag_0_space_ptrtype;
    typedef typename lag_0_space_type::element_type lag_0_element_type;

    //! Pchv space
    typedef Lagrange<1, Vectorial> lag_v_basis_type;
    typedef FunctionSpace<mesh_type, bases<lag_v_basis_type>> lag_v_space_type;
    typedef boost::shared_ptr<lag_v_space_type> lag_v_space_ptrtype;
    typedef typename lag_v_space_type::element_type lag_v_element_type;

    typedef FunctionSpace<mesh_type, bases<curl_basis_type,lag_basis_type>> comp_space_type;
    typedef boost::shared_ptr<comp_space_type> comp_space_ptrtype;
    typedef typename comp_space_type::element_type comp_element_type;

    //! Preconditioners
    typedef PreconditionerBlockMS<comp_space_type,lag_0_space_type> preconditioner_type;
    typedef boost::shared_ptr<preconditioner_type> preconditioner_ptrtype;
    
    //! The exporter factory
    typedef Exporter<mesh_type> export_type;
    typedef boost::shared_ptr<export_type> export_ptrtype;

    //! Backends factory
    typedef Backend<double> backend_type;
    typedef boost::shared_ptr<backend_type> backend_ptrtype;
    typedef backend_type::solve_return_type solve_ret_type;

    public:

    /// Init the geometry with a circle/sphere from radius and characteristic length
    ///     \param radius   Circle or sphere radius.
    ///     \param h        Mesh size.
    TestPrecAFP( ) 
    {
        auto M_mesh = loadMesh(_mesh=new mesh_type);
        auto Xh = comp_space_type::New(M_mesh); // curl x lag
        auto Mh = lag_0_space_type::New(M_mesh); // lag_0
        auto Jh = lag_v_space_type::New(M_mesh); // lag_v

        auto model = ModelProperties(Environment::expand(soption("myModel")));
        auto J = vf::project(_space=Jh,
                             _range=elements(M_mesh),
                             _expr=expr<DIM,1>(soption("functions.j")));
        auto M_mu = vf::project(_space=Mh,
                               _range=elements(M_mesh),
                               _expr=expr(soption("functions.m")));
        auto M_a = vf::project(_space=Xh->template functionSpace<0>(),
                               _range=elements(M_mesh),
                               _expr=expr<DIM,1>(soption("functions.a")));
        
        auto U = Xh->element();
        auto u = U.template element<0>();
        auto v = U.template element<0>();
        auto phi = U.template element<1>();
        auto psi = U.template element<1>();
        
        auto f2 = form2(_test=Xh,_trial=Xh);
        auto f1 = form1(_test=Xh);

        preconditioner_ptrtype M_prec;
       
        map_vector_field<DIM,1,2> m_dirichlet {model.boundaryConditions().getVectorFields<DIM>("u","Dirichlet")};
        map_scalar_field<2> m_dirichlet_phi {model.boundaryConditions().getScalarFields<2>("phi","Dirichlet")};
        
        f1 = integrate(_range=elements(M_mesh),
                       _expr = inner(idv(J),id(v)));    // rhs
        f2 = integrate(_range=elements(M_mesh),
                       _expr = 
                         inner(trans(id(v)),gradt(phi)) // grad(phi)
                       + inner(trans(idt(u)),grad(psi)) // div(u) = 0
                       + (1./idv(M_mu))*(trans(curlt_op(u))*curl_op(v)) // curl curl 
                       );
        for(auto const & it : m_dirichlet)
            f2 += on(_range=markedfaces(M_mesh,it.first),
                     _rhs=f1,
                     _element=u,
                     _expr=it.second);
        for(auto const & it : m_dirichlet_phi)
            f2 += on(_range=markedfaces(M_mesh,it.first),
                     _rhs=f1,
                     _element=phi,
                     _expr=it.second);

        solve_ret_type ret;
        if(soption("ms.pc-type") == "blockms" ){
            // auto M_prec = blockms(
            //    _space = Xh,
            //    _space2 = Mh, 
            //    _matrix = f2.matrixPtr(),
            //    _bc = model.boundaryConditions());

            M_prec = boost::make_shared<PreconditionerBlockMS<comp_space_type,lag_0_space_type>>(
                soption("blockms.type"),
                Xh, Mh,
                model.boundaryConditions(),
                "",
                f2.matrixPtr());

            M_prec->update(f2.matrixPtr(),M_mu);
            tic();
            ret = f2.solveb(_rhs=f1,
                      _solution=U,
                      _backend=backend(_name="ms"),
                      _prec=M_prec);
            toc("Inverse",FLAGS_v>0);
        }else{
            tic();
            ret = f2.solveb(_rhs=f1,
                      _solution=U,
                      _backend=backend(_name="ms"));
            toc("Inverse",FLAGS_v>0);
        }
        Environment::saveTimers(boption("saveTimers")); 
        auto e21 = normL2(_range=elements(M_mesh), _expr=(idv(M_a)-idv(u)));
        auto e22 = normL2(_range=elements(M_mesh), _expr=(idv(M_a)));
        if(Environment::worldComm().globalRank()==0)
            std::cout << doption("gmsh.hsize") << "\t"
                << e21 << "\t"
                << e21/e22 << std::endl;
        /* report */
        time_t now = std::time(0);
        tm *ltm = localtime(&now);
        std::ostringstream stringStream;
        stringStream << 1900+ltm->tm_year<<"_"<<ltm->tm_mon<<"_"<<ltm->tm_mday<<"-"<<ltm->tm_hour<<ltm->tm_min<<ltm->tm_sec<<".md";
        std::ofstream outputFile( stringStream.str() );
        if( outputFile )
        {
            outputFile 
                    << "---\n"
                    << "title: \"noTitle\"\n"
                    << "date: " << stringStream.str() << "\n"
                    << "categories: simu\n"
                    << "--- \n\n";
            outputFile << "#Physique" << std::endl;
            model.saveMD(outputFile);    
            
            outputFile << "##Physique spécifique" << std::endl;
            outputFile << "| Variable | value | " << std::endl;
            outputFile << "|---|---|" << std::endl;
            outputFile << "| mu | " << expr(soption("functions.m")) << " | " << std::endl;
            outputFile << "| Rhs | " << expr<DIM,1>(soption("functions.j")) << "|" << std::endl;
            outputFile << "| Exact | " << expr<DIM,1>(soption("functions.a")) << "|" << std::endl;

            outputFile << "#Numerics" << std::endl;
            
            outputFile << "##Mesh" << std::endl;
            M_mesh->saveMD(outputFile); 
           
            outputFile << "##Spaces" << std::endl;
            outputFile << "|qDim|" << Xh->qDim()      << "|" << Xh->template functionSpace<1>()->qDim()      << "|" << Xh->template functionSpace<1>()->qDim()      << "|" << std::endl;
            outputFile << "|---|---|---|---|" << std::endl;
            outputFile << "|BasisName|"<< Xh->basisName() << "|" << Xh->template functionSpace<0>()->basisName() << "|" << Xh->template functionSpace<1>()->basisName() << "|" << std::endl;
            outputFile << "|nDof|" << Xh->nDof()      << "|"<< Xh->template functionSpace<0>()->nDof()      << "|"<< Xh->template functionSpace<1>()->nDof()      << "|" << std::endl;
            outputFile << "|nLocaldof|" << Xh->nLocalDof() << "|" << Xh->template functionSpace<0>()->nLocalDof() << "|" << Xh->template functionSpace<1>()->nLocalDof() << "|" << std::endl;
            outputFile << "|nPerComponent|" << Xh->nDofPerComponent() << "|" << Xh->template functionSpace<0>()->nDofPerComponent() << "|" << Xh->template functionSpace<1>()->nDofPerComponent() << "|" << std::endl;
            
            outputFile << "##Solvers" << std::endl;

            outputFile << "| x | ms | blocksms.11 | blockms.22 |" << std::endl;
            outputFile << "|---|---|---|---| " << std::endl;
            outputFile << "|**ksp-type** |  " << soption("ms.ksp-type") << "| " << soption("blockms.11.ksp-type") << "| " << soption("blockms.22.ksp-type") << "|" << std::endl;
            outputFile << "|**pc-type**  |  " << soption("ms.pc-type")  << "| " << soption("blockms.11.pc-type")  << "| " << soption("blockms.22.pc-type")  << "|" << std::endl;
            outputFile << "|**on-type**  |  " << soption("on.type")  << "| " << soption("blockms.11.on.type")  << "| " << soption("blockms.22.on.type")  << "|" << std::endl;
            outputFile << "|**Matrix**  |  " << f2.matrixPtr()->graph()->size() << "| "; M_prec->printMatSize(1,outputFile); outputFile << "| "; M_prec->printMatSize(2,outputFile);outputFile  << "|" << std::endl;
            outputFile << "|**nb Iter**  |  " << ret.nIterations() << "| "; M_prec->printIter(1,outputFile); outputFile << "| "; M_prec->printIter(2,outputFile);outputFile  << "|" << std::endl;
        
            outputFile << "##Timers" << std::endl;
            Environment::saveTimersMD(outputFile); 
        }
        else
        {
           std::cerr << "Failure opening " << stringStream.str() << '\n';
        }
        /* end of report */
        // export
        if(boption("exporter.export")){
            auto ex = exporter(_mesh=M_mesh);
            ex->add("rhs"                ,J  );
            ex->add("potential"          ,u  );
            ex->add("exact_potential"    ,M_a);
            ex->add("lagrange_multiplier",phi);
            ex->save();
        }
    }

private:
    mesh_ptrtype M_mesh;
};

#if defined(USE_BOOST_TEST)
FEELPP_ENVIRONMENT_WITH_OPTIONS( makeAbout(), feel_options() );
BOOST_AUTO_TEST_SUITE( precAFP )

BOOST_AUTO_TEST_CASE( test )
{
    TestPrecAFP<FM_DIM> test;
}

//// Test 3D
//BOOST_AUTO_TEST_CASE( test_3d )
//{
//    TestPrecAFP<3> test;
//}

BOOST_AUTO_TEST_SUITE_END()

#else
int main(int argc, char** argv )
{
    Feel::Environment env( _argc=argc, _argv=argv,
                           _about=makeAbout(),
                           _desc=makeOptions() );
    
    TestPrecAFP<FM_DIM> t_afp;

    return 0;
}
#endif
