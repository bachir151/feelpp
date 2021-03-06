/* -*- mode: c++; coding: utf-8; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; show-trailing-whitespace: t  -*-
 
 This file is part of the Feel++ library
 
 Author(s): Christophe Prud'homme <christophe.prudhomme@feelpp.org>
 Date: 25 Jan 2015
 
 Copyright (C) 2015 Feel++ Consortium
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <feel/feelcore/feel.hpp>
#include <feel/feelcore/environment.hpp>
#include <feel/feelpde/boundaryconditions.hpp>

namespace Feel
{

BoundaryConditions::BoundaryConditions()
    :
    BoundaryConditions( "" )
{}

BoundaryConditions::BoundaryConditions( std::string const& p )
    :
    super(),
    M_prefix( p )
{
    fs::path bc( Environment::expand( soption("bc-file") ) );
    
    if ( fs::exists( bc ) )
    {
        LOG(INFO) << "Loading Boundary Condition file " << bc.string();
        load( bc.string() );
    }
    else
    {
        LOG(WARNING) << "Boundary condition file " << bc.string() << " does not exist";
    }
}

void
BoundaryConditions::load(const std::string &filename)
{
    // Create an empty property tree object
    using boost::property_tree::ptree;

    read_json(filename, M_pt);
    setup();
}
void
BoundaryConditions::setPTree( pt::ptree const& p )
{
    M_pt = p;
    setup();
}


void
BoundaryConditions::setup()
{
    for( auto const& v : M_pt )
    {
        
        //std::cout << "v.first:" << v.first  << "\n";
        std::string t = v.first; // field name
        for( auto const& f : v.second )
        {
            std::string k = t+"."+f.first; // condition type
            for( auto const& c : f.second ) // condition
            {
                try
                {
                    auto e= c.second.get<std::string>("expr");
                    LOG(INFO) << "adding boundary " << c.first << " with expression " << e << " to " << k;
                    this->operator[](t)[f.first].push_back( std::make_tuple( c.first, e, std::string("") ) );
                }
                catch( ... )
                {
                    try
                    {
                        auto e1= c.second.get<std::string>("expr1");
                        auto e2= c.second.get<std::string>("expr2");
                        LOG(INFO) << "adding boundary " << c.first << " with expressions " << e1 << " and " << e2 << " to " << k;
                        this->operator[](t)[f.first].push_back( std::make_tuple( c.first, e1, e2 ) );
                    }
                    catch( ... )
                    {
                        LOG(INFO) << "adding boundary " << c.first << " without expression" << " to " << k;
                        this->operator[]( t )[f.first].push_back( std::make_tuple( c.first, std::string(""), std::string("") ) );
                    }
                }
            }
        }
        
    }
    if ( Environment::isMasterRank() )
    {
        for( auto const& s : *this )
        {
            LOG(INFO) << "field " << s.first << "\n";
            for( auto const& t : s.second )
            {
                LOG(INFO) << " - type " << t.first << "\n";
                for( auto const& c : t.second )
                {
                    if ( c.hasExpression2() )
                        LOG(INFO) << "  . boundary  " << c.marker() << " expr : " << c.expression1() << " expr2:" << c.expression2() << "\n";
                    else
                        LOG(INFO) << "  . boundary  " << c.marker() << " expr : " << c.expression() << "\n";
                }
                
            }
        }
    }
}
    

void
BoundaryConditions::saveMD(std::ostream &os)
{
  os << "### Boundary Conditions\n";
  os << "|Name|Type|Expressions|\n";
  os << "|---|---|---|\n";
  for (auto it = this->begin(); it!= this->end(); it++)
  {
    os << "|**" << it->first << "**"; // Var name
    for(auto iit = it->second.begin(); iit !=  it->second.end(); iit++)
    {
     os << "|" << iit->first; // Type
     os << "|<ul>";
     for(auto iiit = iit->second.begin(); iiit !=  iit->second.end(); iiit++)
     {
       os << "<li>**" << iiit->marker()      << "**</li>";
       os << "<li>" << iiit->expression()  << "</li>";
       os << "<li>" << iiit->expression1() << "</li>";
       os << "<li>" << iiit->expression2() << "</li>";
     }
    }
    os << "</ul>|\n";
  }
  os << "\n";
}

}//Feel
