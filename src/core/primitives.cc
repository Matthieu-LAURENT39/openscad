/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "module.h"
#include "core/node.h"
#include "PolySet.h"
#include "PolySetBuilder.h"
#include "Children.h"
#include "Polygon2d.h"
#include "Builtins.h"
#include "Parameters.h"
#include "printutils.h"
#include "calc.h"
#include "degree_trig.h"
#include <sstream>
#include <cassert>
#include <cmath>
#include <boost/assign/std/vector.hpp>
#include "ModuleInstantiation.h"
#include "primitives.h"
using namespace boost::assign; // bring 'operator+=()' into scope

#define F_MINIMUM 0.01

static void generate_circle(Vector2d *circle, double r, int fragments)
{
  for (int i = 0; i < fragments; ++i) {
    double phi = (360.0 * i) / fragments;
    circle[i][0] = r * cos_degrees(phi);
    circle[i][1] = r * sin_degrees(phi);
  }
}

/**
 * Return a radius value by looking up both a diameter and radius variable.
 * The diameter has higher priority, so if found an additionally set radius
 * value is ignored.
 *
 * @param parameters parameters with variable values.
 * @param inst containing instantiation.
 * @param radius_var name of the variable to lookup for the radius value.
 * @param diameter_var name of the variable to lookup for the diameter value.
 * @return radius value of type Value::Type::NUMBER or Value::Type::UNDEFINED if both
 *         variables are invalid or not set.
 */
static Value lookup_radius(const Parameters& parameters, const ModuleInstantiation *inst, const std::string& diameter_var, const std::string& radius_var)
{
  const auto& d = parameters[diameter_var];
  const auto& r = parameters[radius_var];
  const auto r_defined = (r.type() == Value::Type::NUMBER);

  if (d.type() == Value::Type::NUMBER) {
    if (r_defined) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "Ignoring radius variable '%1$s' as diameter '%2$s' is defined too.", radius_var, diameter_var);
    }
    return d.toDouble() / 2.0;
  } else if (r_defined) {
    return r.clone();
  } else {
    return Value::undefined.clone();
  }
}

static void set_fragments(const Parameters& parameters, const ModuleInstantiation *inst, double& fn, double& fs, double& fa)
{
  fn = parameters["$fn"].toDouble();
  fs = parameters["$fs"].toDouble();
  fa = parameters["$fa"].toDouble();

  if (fs < F_MINIMUM) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "$fs too small - clamping to %1$f", F_MINIMUM);
    fs = F_MINIMUM;
  }
  if (fa < F_MINIMUM) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "$fa too small - clamping to %1$f", F_MINIMUM);
    fa = F_MINIMUM;
  }
}



const Geometry *CubeNode::createGeometry() const
{
  if (
    this->x <= 0 || !std::isfinite(this->x)
    || this->y <= 0 || !std::isfinite(this->y)
    || this->z <= 0 || !std::isfinite(this->z)
    ) {
    return new PolySet(3, true);
  }

  double x1, x2, y1, y2, z1, z2;
  if (this->center) {
    x1 = -this->x / 2;
    x2 = +this->x / 2;
    y1 = -this->y / 2;
    y2 = +this->y / 2;
    z1 = -this->z / 2;
    z2 = +this->z / 2;
  } else {
    x1 = y1 = z1 = 0;
    x2 = this->x;
    y2 = this->y;
    z2 = this->z;
  }

  PolySetBuilder builder(8,6);
  int corner[8];
  for(int i=0;i<8;i++)
    corner[i]=builder.vertexIndex(Vector3d(i&1?x2:x1,i&2?y2:y1,i&4?z2:z1));

  builder.append_poly({corner[4],corner[5],corner[7], corner[6]}); // top
  builder.append_poly({corner[2],corner[3],corner[1], corner[0]}); // bottom
  builder.append_poly({corner[0],corner[1],corner[5], corner[4]}); // front
  builder.append_poly({corner[1],corner[3],corner[7], corner[5]}); // right
  builder.append_poly({corner[3],corner[2],corner[6], corner[7]}); // back
  builder.append_poly({corner[2],corner[0],corner[4], corner[6]}); // left
  return builder.result().get();									   
}

static std::shared_ptr<AbstractNode> builtin_cube(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<CubeNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"size", "center"});

  const auto& size = parameters["size"];
  if (size.isDefined()) {
    bool converted = false;
    converted |= size.getDouble(node->x);
    converted |= size.getDouble(node->y);
    converted |= size.getDouble(node->z);
    converted |= size.getVec3(node->x, node->y, node->z);
    if (!converted) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "Unable to convert cube(size=%1$s, ...) parameter to a number or a vec3 of numbers", size.toEchoStringNoThrow());
    } else if (OpenSCAD::rangeCheck) {
      bool ok = (node->x > 0) && (node->y > 0) && (node->z > 0);
      ok &= std::isfinite(node->x) && std::isfinite(node->y) && std::isfinite(node->z);
      if (!ok) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "cube(size=%1$s, ...)", size.toEchoStringNoThrow());
      }
    }
  }
  if (parameters["center"].type() == Value::Type::BOOL) {
    node->center = parameters["center"].toBool();
  }

  return node;
}





const Geometry *SphereNode::createGeometry() const
{
  if (this->r <= 0 || !std::isfinite(this->r)) {
    return  new PolySet(3, true);
  }

  struct ring_s {
    std::vector<Vector2d> points;
    double z;
  };

  auto fragments = Calc::get_fragments_from_r(r, fn, fs, fa);
  int rings = (fragments + 1) / 2;
  PolySetBuilder builder(0,rings * fragments + 2);
// Uncomment the following three lines to enable experimental sphere tessellation
//	if (rings % 2 == 0) rings++; // To ensure that the middle ring is at phi == 0 degrees

  auto ring = std::vector<ring_s>(rings);

//	double offset = 0.5 * ((fragments / 2) % 2);
  for (int i = 0; i < rings; ++i) {
//		double phi = (180.0 * (i + offset)) / (fragments/2);
    double phi = (180.0 * (i + 0.5)) / rings;
    double radius = r * sin_degrees(phi);
    ring[i].z = r * cos_degrees(phi);
    ring[i].points.resize(fragments);
    generate_circle(ring[i].points.data(), radius, fragments);
  }

  builder.append_poly(fragments);
  for (int i = 0; i < fragments; ++i)
    builder.append_vertex(builder.vertexIndex(Vector3d(ring[0].points[i][0], ring[0].points[i][1], ring[0].z)));

  int ind1,ind2,ind3;
  for (int i = 0; i < rings - 1; ++i) {
    auto r1 = &ring[i];
    auto r2 = &ring[i + 1];
    int r1i = 0, r2i = 0;
    while (r1i < fragments || r2i < fragments) {
      if (r1i >= fragments) goto sphere_next_r2;
      if (r2i >= fragments) goto sphere_next_r1;
      if ((double)r1i / fragments < (double)r2i / fragments) {
sphere_next_r1:
        int r1j = (r1i + 1) % fragments;
	ind1=builder.vertexIndex(Vector3d(r2->points[r2i % fragments][0], r2->points[r2i % fragments][1], r2->z));
	ind2=builder.vertexIndex(Vector3d(r1->points[r1j][0], r1->points[r1j][1], r1->z));
	ind3=builder.vertexIndex(Vector3d(r1->points[r1i][0], r1->points[r1i][1], r1->z));
        builder.append_poly({ind1,ind2,ind3});
        r1i++;
      } else {
sphere_next_r2:
        int r2j = (r2i + 1) % fragments;
	ind1=builder.vertexIndex(Vector3d(r2->points[r2i][0], r2->points[r2i][1], r2->z));
	ind2=builder.vertexIndex(Vector3d(r2->points[r2j][0], r2->points[r2j][1], r2->z));
	ind3=builder.vertexIndex(Vector3d(r1->points[r1i % fragments][0], r1->points[r1i % fragments][1], r1->z));
        builder.append_poly({ind1,ind2,ind3});
        r2i++;
      }
    }
  }

  builder.append_poly(fragments);
  for (int i = 0; i < fragments; ++i) {
    builder.prepend_vertex( builder.vertexIndex(Vector3d(ring[rings - 1].points[i][0], ring[rings - 1].points[i][1], ring[rings - 1].z)));
  }

  return builder.result().get();
}

static std::shared_ptr<AbstractNode> builtin_sphere(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<SphereNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"r"}, {"d"});

  set_fragments(parameters, inst, node->fn, node->fs, node->fa);
  const auto r = lookup_radius(parameters, inst, "d", "r");
  if (r.type() == Value::Type::NUMBER) {
    node->r = r.toDouble();
    if (OpenSCAD::rangeCheck && (node->r <= 0 || !std::isfinite(node->r))) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "sphere(r=%1$s)", r.toEchoStringNoThrow());
    }
  }

  return node;
}



const Geometry *CylinderNode::createGeometry() const
{
  if (
    this->h <= 0 || !std::isfinite(this->h)
    || this->r1 < 0 || !std::isfinite(this->r1)
    || this->r2 < 0 || !std::isfinite(this->r2)
    || (this->r1 <= 0 && this->r2 <= 0)
    ) {
    return  new PolySet(3, true);
  }

  auto fragments = Calc::get_fragments_from_r(std::fmax(this->r1, this->r2), this->fn, this->fs, this->fa);

  double z1, z2;
  if (this->center) {
    z1 = -this->h / 2;
    z2 = +this->h / 2;
  } else {
    z1 = 0;
    z2 = this->h;
  }

  auto circle1 = std::vector<Vector2d>(fragments);
  auto circle2 = std::vector<Vector2d>(fragments);

  generate_circle(circle1.data(), r1, fragments);
  generate_circle(circle2.data(), r2, fragments);

  PolySetBuilder builder(0,fragments * 2 + 2);
  
  int ind,ind1,ind2,ind3;
  for (int i = 0; i < fragments; ++i) {
    int j = (i + 1) % fragments;
    if (r1 == r2) {
      builder.append_poly(4);
      for(int k=0;k<4;k++)     		      
        builder.prepend_vertex(builder.vertexIndex(Vector3d(circle1[k&2?j:i][0], circle1[k&2?j:i][1], (k+1)&2?z2:z1)));
    } else {
      ind1=builder.vertexIndex(Vector3d(circle1[j][0], circle1[j][1], z1));
      if (r1 > 0) {
	ind2=builder.vertexIndex(Vector3d(circle2[i][0], circle2[i][1], z2));
	ind3=builder.vertexIndex(Vector3d(circle1[i][0], circle1[i][1], z1));
        builder.append_poly({ind1,ind2,ind3});
      }
      if (r2 > 0) {
	ind2=builder.vertexIndex(Vector3d(circle2[j][0], circle2[j][1], z2));
	ind3=builder.vertexIndex(Vector3d(circle2[i][0], circle2[i][1], z2));
        builder.append_poly({ind1,ind2,ind3});
      }
    }
  }

  if (this->r1 > 0) {
    builder.append_poly(fragments);
    for (int i = 0; i < fragments; ++i)
      builder.prepend_vertex(builder.vertexIndex(Vector3d(circle1[i][0], circle1[i][1], z1)));
  }

  if (this->r2 > 0) {
    builder.append_poly(fragments);
    for (int i = 0; i < fragments; ++i)
      builder.append_vertex(builder.vertexIndex(Vector3d(circle2[i][0], circle2[i][1], z2)));
  }

  return builder.result().get();
}

static std::shared_ptr<AbstractNode> builtin_cylinder(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<CylinderNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"h", "r1", "r2", "center"}, {"r", "d", "d1", "d2"});

  set_fragments(parameters, inst, node->fn, node->fs, node->fa);
  if (parameters["h"].type() == Value::Type::NUMBER) {
    node->h = parameters["h"].toDouble();
  }

  auto r = lookup_radius(parameters, inst, "d", "r");
  auto r1 = lookup_radius(parameters, inst, "d1", "r1");
  auto r2 = lookup_radius(parameters, inst, "d2", "r2");
  if (r.type() == Value::Type::NUMBER &&
      (r1.type() == Value::Type::NUMBER || r2.type() == Value::Type::NUMBER)
      ) {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "Cylinder parameters ambiguous");
  }

  if (r.type() == Value::Type::NUMBER) {
    node->r1 = r.toDouble();
    node->r2 = r.toDouble();
  }
  if (r1.type() == Value::Type::NUMBER) {
    node->r1 = r1.toDouble();
  }
  if (r2.type() == Value::Type::NUMBER) {
    node->r2 = r2.toDouble();
  }

  if (OpenSCAD::rangeCheck) {
    if (node->h <= 0 || !std::isfinite(node->h)) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "cylinder(h=%1$s, ...)", parameters["h"].toEchoStringNoThrow());
    }
    if (node->r1 < 0 || node->r2 < 0 || (node->r1 == 0 && node->r2 == 0) || !std::isfinite(node->r1) || !std::isfinite(node->r2)) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "cylinder(r1=%1$s, r2=%2$s, ...)",
          (r1.type() == Value::Type::NUMBER ? r1.toEchoStringNoThrow() : r.toEchoStringNoThrow()),
          (r2.type() == Value::Type::NUMBER ? r2.toEchoStringNoThrow() : r.toEchoStringNoThrow()));
    }
  }

  if (parameters["center"].type() == Value::Type::BOOL) {
    node->center = parameters["center"].toBool();
  }

  return node;
}


std::string PolyhedronNode::toString() const
{
  std::ostringstream stream;
  stream << "polyhedron(points = [";
  bool firstPoint = true;
  for (const auto& point : this->points) {
    if (firstPoint) {
      firstPoint = false;
    } else {
      stream << ", ";
    }
    stream << "[" << point[0] << ", " << point[1] << ", " << point[2] << "]";
  }
  stream << "], faces = [";
  bool firstFace = true;
  for (const auto& face : this->faces) {
    if (firstFace) {
      firstFace = false;
    } else {
      stream << ", ";
    }
    stream << "[";
    bool firstIndex = true;
    for (const auto& index : face) {
      if (firstIndex) {
        firstIndex = false;
      } else {
        stream << ", ";
      }
      stream << index;
    }
    stream << "]";
  }
  stream << "], convexity = " << this->convexity << ")";
  return stream.str();
}

const Geometry *PolyhedronNode::createGeometry() const
{
  auto p = new PolySet(3);
  p->setConvexity(this->convexity);
  p->vertices=this->points;
  p->indices=this->faces;
  for (auto &poly : p->indices) 
    std::reverse(poly.begin(),poly.end());
  return p;
}

static std::shared_ptr<AbstractNode> builtin_polyhedron(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<PolyhedronNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"points", "faces", "convexity"}, {"triangles"});

  if (parameters["points"].type() != Value::Type::VECTOR) {
    LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert points = %1$s to a vector of coordinates", parameters["points"].toEchoStringNoThrow());
    return node;
  }
  node->points.reserve(parameters["points"].toVector().size());
  for (const Value& pointValue : parameters["points"].toVector()) {
    Vector3d point;
    if (!pointValue.getVec3(point[0], point[1], point[2], 0.0) ||
        !std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])
        ) {
      LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert points[%1$d] = %2$s to a vec3 of numbers", node->points.size(), pointValue.toEchoStringNoThrow());
      node->points.push_back({0, 0, 0});
    } else {
      node->points.push_back(point);
    }
  }

  const Value *faces = nullptr;
  if (parameters["faces"].type() == Value::Type::UNDEFINED && parameters["triangles"].type() != Value::Type::UNDEFINED) {
    // backwards compatible
    LOG(message_group::Deprecated, inst->location(), parameters.documentRoot(), "polyhedron(triangles=[]) will be removed in future releases. Use polyhedron(faces=[]) instead.");
    faces = &parameters["triangles"];
  } else {
    faces = &parameters["faces"];
  }
  if (faces->type() != Value::Type::VECTOR) {
    LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert faces = %1$s to a vector of vector of point indices", faces->toEchoStringNoThrow());
    return node;
  }
  size_t faceIndex = 0;
  node->faces.reserve(faces->toVector().size());
  for (const Value& faceValue : faces->toVector()) {
    if (faceValue.type() != Value::Type::VECTOR) {
      LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert faces[%1$d] = %2$s to a vector of numbers", faceIndex, faceValue.toEchoStringNoThrow());
    } else {
      size_t pointIndexIndex = 0;
      IndexedFace face;
      for (const Value& pointIndexValue : faceValue.toVector()) {
        if (pointIndexValue.type() != Value::Type::NUMBER) {
          LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert faces[%1$d][%2$d] = %3$s to a number", faceIndex, pointIndexIndex, pointIndexValue.toEchoStringNoThrow());
        } else {
          auto pointIndex = (size_t)pointIndexValue.toDouble();
          if (pointIndex < node->points.size()) {
            face.push_back(pointIndex);
          } else {
            LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "Point index %1$d is out of bounds (from faces[%2$d][%3$d])", pointIndex, faceIndex, pointIndexIndex);
          }
        }
        pointIndexIndex++;
      }
      if (face.size() >= 3) {
        node->faces.push_back(std::move(face));
      }
    }
    faceIndex++;
  }

  node->convexity = (int)parameters["convexity"].toDouble();
  if (node->convexity < 1) node->convexity = 1;

  return node;
}


const Geometry *SquareNode::createGeometry() const
{
  auto p = new Polygon2d();
  if (
    this->x <= 0 || !std::isfinite(this->x)
    || this->y <= 0 || !std::isfinite(this->y)
    ) {
    return p;
  }

  Vector2d v1(0, 0);
  Vector2d v2(this->x, this->y);
  if (this->center) {
    v1 -= Vector2d(this->x / 2, this->y / 2);
    v2 -= Vector2d(this->x / 2, this->y / 2);
  }

  Outline2d o;
  o.vertices = {v1, {v2[0], v1[1]}, v2, {v1[0], v2[1]}};
  p->addOutline(o);
  p->setSanitized(true);
  return p;
}

static std::shared_ptr<AbstractNode> builtin_square(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<SquareNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"size", "center"});

  const auto& size = parameters["size"];
  if (size.isDefined()) {
    bool converted = false;
    converted |= size.getDouble(node->x);
    converted |= size.getDouble(node->y);
    converted |= size.getVec2(node->x, node->y);
    if (!converted) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "Unable to convert square(size=%1$s, ...) parameter to a number or a vec2 of numbers", size.toEchoStringNoThrow());
    } else if (OpenSCAD::rangeCheck) {
      bool ok = true;
      ok &= (node->x > 0) && (node->y > 0);
      ok &= std::isfinite(node->x) && std::isfinite(node->y);
      if (!ok) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "square(size=%1$s, ...)", size.toEchoStringNoThrow());
      }
    }
  }
  if (parameters["center"].type() == Value::Type::BOOL) {
    node->center = parameters["center"].toBool();
  }

  return node;
}

const Geometry *CircleNode::createGeometry() const
{
  auto p = new Polygon2d();
  if (this->r <= 0 || !std::isfinite(this->r)) {
    return p;
  }

  auto fragments = Calc::get_fragments_from_r(this->r, this->fn, this->fs, this->fa);
  Outline2d o;
  o.vertices.resize(fragments);
  for (int i = 0; i < fragments; ++i) {
    double phi = (360.0 * i) / fragments;
    o.vertices[i] = {this->r * cos_degrees(phi), this->r * sin_degrees(phi)};
  }
  p->addOutline(o);
  p->setSanitized(true);
  return p;
}

static std::shared_ptr<AbstractNode> builtin_circle(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<CircleNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"r"}, {"d"});

  set_fragments(parameters, inst, node->fn, node->fs, node->fa);
  const auto r = lookup_radius(parameters, inst, "d", "r");
  if (r.type() == Value::Type::NUMBER) {
    node->r = r.toDouble();
    if (OpenSCAD::rangeCheck && ((node->r <= 0) || !std::isfinite(node->r))) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "circle(r=%1$s)", r.toEchoStringNoThrow());
    }
  }

  return node;
}



std::string PolygonNode::toString() const
{
  std::ostringstream stream;
  stream << "polygon(points = [";
  bool firstPoint = true;
  for (const auto& point : this->points) {
    if (firstPoint) {
      firstPoint = false;
    } else {
      stream << ", ";
    }
    stream << "[" << point[0] << ", " << point[1] << "]";
  }
  stream << "], paths = ";
  if (this->paths.empty()) {
    stream << "undef";
  } else {
    stream << "[";
    bool firstPath = true;
    for (const auto& path : this->paths) {
      if (firstPath) {
        firstPath = false;
      } else {
        stream << ", ";
      }
      stream << "[";
      bool firstIndex = true;
      for (const auto& index : path) {
        if (firstIndex) {
          firstIndex = false;
        } else {
          stream << ", ";
        }
        stream << index;
      }
      stream << "]";
    }
    stream << "]";
  }
  stream << ", convexity = " << this->convexity << ")";
  return stream.str();
}

const Geometry *PolygonNode::createGeometry() const
{
  auto p = new Polygon2d();
  if (this->paths.empty() && this->points.size() > 2) {
    Outline2d outline;
    for (const auto& point : this->points) {
      outline.vertices.emplace_back(point[0], point[1]);
    }
    p->addOutline(outline);
  } else {
    for (const auto& path : this->paths) {
      Outline2d outline;
      for (const auto& index : path) {
        assert(index < this->points.size());
        const auto& point = points[index];
        outline.vertices.emplace_back(point[0], point[1]);
      }
      p->addOutline(outline);
    }
  }
  if (p->outlines().size() > 0) {
    p->setConvexity(convexity);
  }
  return p;
}

static std::shared_ptr<AbstractNode> builtin_polygon(const ModuleInstantiation *inst, Arguments arguments, const Children& children)
{
  auto node = std::make_shared<PolygonNode>(inst);

  if (!children.empty()) {
    LOG(message_group::Warning, inst->location(), arguments.documentRoot(),
        "module %1$s() does not support child modules", node->name());
  }

  Parameters parameters = Parameters::parse(std::move(arguments), inst->location(), {"points", "paths", "convexity"});

  if (parameters["points"].type() != Value::Type::VECTOR) {
    LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert points = %1$s to a vector of coordinates", parameters["points"].toEchoStringNoThrow());
    return node;
  }
  for (const Value& pointValue : parameters["points"].toVector()) {
    Vector2d point;
    if (!pointValue.getVec2(point[0], point[1]) ||
        !std::isfinite(point[0]) || !std::isfinite(point[1])
        ) {
      LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert points[%1$d] = %2$s to a vec2 of numbers", node->points.size(), pointValue.toEchoStringNoThrow());
      node->points.push_back({0, 0});
    } else {
      node->points.push_back(point);
    }
  }

  if (parameters["paths"].type() == Value::Type::VECTOR) {
    size_t pathIndex = 0;
    for (const Value& pathValue : parameters["paths"].toVector()) {
      if (pathValue.type() != Value::Type::VECTOR) {
        LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert paths[%1$d] = %2$s to a vector of numbers", pathIndex, pathValue.toEchoStringNoThrow());
      } else {
        size_t pointIndexIndex = 0;
        std::vector<size_t> path;
        for (const Value& pointIndexValue : pathValue.toVector()) {
          if (pointIndexValue.type() != Value::Type::NUMBER) {
            LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert paths[%1$d][%2$d] = %3$s to a number", pathIndex, pointIndexIndex, pointIndexValue.toEchoStringNoThrow());
          } else {
            auto pointIndex = (size_t)pointIndexValue.toDouble();
            if (pointIndex < node->points.size()) {
              path.push_back(pointIndex);
            } else {
              LOG(message_group::Warning, inst->location(), parameters.documentRoot(), "Point index %1$d is out of bounds (from paths[%2$d][%3$d])", pointIndex, pathIndex, pointIndexIndex);
            }
          }
          pointIndexIndex++;
        }
        node->paths.push_back(std::move(path));
      }
      pathIndex++;
    }
  } else if (parameters["paths"].type() != Value::Type::UNDEFINED) {
    LOG(message_group::Error, inst->location(), parameters.documentRoot(), "Unable to convert paths = %1$s to a vector of vector of point indices", parameters["paths"].toEchoStringNoThrow());
    return node;
  }

  node->convexity = (int)parameters["convexity"].toDouble();
  if (node->convexity < 1) node->convexity = 1;

  return node;
}



void register_builtin_primitives()
{
  Builtins::init("cube", new BuiltinModule(builtin_cube),
  {
    "cube(size)",
    "cube([width, depth, height])",
    "cube([width, depth, height], center = true)",
  });

  Builtins::init("sphere", new BuiltinModule(builtin_sphere),
  {
    "sphere(radius)",
    "sphere(r = radius)",
    "sphere(d = diameter)",
  });

  Builtins::init("cylinder", new BuiltinModule(builtin_cylinder),
  {
    "cylinder(h, r1, r2)",
    "cylinder(h = height, r = radius, center = true)",
    "cylinder(h = height, r1 = bottom, r2 = top, center = true)",
    "cylinder(h = height, d = diameter, center = true)",
    "cylinder(h = height, d1 = bottom, d2 = top, center = true)",
  });

  Builtins::init("polyhedron", new BuiltinModule(builtin_polyhedron),
  {
    "polyhedron(points, faces, convexity)",
  });

  Builtins::init("square", new BuiltinModule(builtin_square),
  {
    "square(size, center = true)",
    "square([width,height], center = true)",
  });

  Builtins::init("circle", new BuiltinModule(builtin_circle),
  {
    "circle(radius)",
    "circle(r = radius)",
    "circle(d = diameter)",
  });

  Builtins::init("polygon", new BuiltinModule(builtin_polygon),
  {
    "polygon([points])",
    "polygon([points], [paths])",
  });
}
