/*
   Copyright (c) 2003-2016, Adrian Rossiter, Roger Kaufman
   Includes ideas and algorithms by George W. Hart, http://www.georgehart.com

   Antiprism - http://www.antiprism.com

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be included
      in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/

/*
   Name: canonic.cc
   Description: canonicalize a polyhedron
                Implementation of George Hart's canonicalization algorithm
                http://library.wolfram.com/infocenter/Articles/2012/
   Project: Antiprism - http://www.antiprism.com
*/

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "boundbox.h"
#include "geometry.h"
#include "geometryinfo.h"
#include "planar.h"

using std::map;
using std::string;
using std::vector;

namespace anti {

// RK - find nearpoints radius, sets range minimum and maximum
double edge_nearpoints_radius(const Geometry &geom, double &min, double &max,
                              Vec3d &center)
{
  min = DBL_MAX;
  max = DBL_MIN;

  vector<vector<int>> edges;
  geom.get_impl_edges(edges);

  vector<Vec3d> near_pts;

  double nearpt_radius = 0;
  int e_sz = edges.size();
  for (auto &edge : edges) {
    Vec3d P = geom.edge_nearpt(edge, Vec3d(0, 0, 0));
    near_pts.push_back(P);

    double l = P.len();
    nearpt_radius += l;
    if (l < min)
      min = l;
    if (l > max)
      max = l;
  }

  center = centroid(near_pts);

  return nearpt_radius / double(e_sz);
}

// RK - wrapper
double edge_nearpoints_radius(const Geometry &geom)
{
  double min = 0;
  double max = 0;
  Vec3d center;
  return edge_nearpoints_radius(geom, min, max, center);
}

// sets radius of geom to average of edge near points radius
void unitize_nearpoints_radius(Geometry &geom)
{
  double avg = edge_nearpoints_radius(geom);
  geom.transform(Trans3d::scale(1 / avg));
}

// return true if maximum vertex radius is radius_range_percent (0.0 to ...)
// greater than minimum vertex radius (visible for canonical.cc)
bool canonical_radius_range_test(const Geometry &geom,
                                 const double radius_range_percent)
{
  GeometryInfo rep(geom);
  rep.set_center(geom.centroid());

  double min = rep.vert_dist_lims().min;
  double max = rep.vert_dist_lims().max;

  // min and max should always be positive, max should always be larger
  return (((max - min) / ((max + min) / 2.0)) > radius_range_percent) ? true
                                                                      : false;
}

// Implementation of George Hart's canonicalization algorithm
// http://library.wolfram.com/infocenter/Articles/2012/
// RK - the model will possibly become non-convex early in the loops.
// if it contorts too badly, the model will implode. Having the input
// model at a radius of near 1 minimizes this problem
bool canonicalize_mm(Geometry &geom, const double edge_factor,
                     const double plane_factor, const int num_iters,
                     const double radius_range_percent, const int rep_count,
                     const bool alternate_loop, const bool planar_only,
                     const char normal_type, const double eps)
{
  bool completed = false;

  vector<Vec3d> &verts = geom.raw_verts();

  vector<vector<int>> edges;
  geom.get_impl_edges(edges);

  double max_diff2 = 0;
  unsigned int cnt;
  for (cnt = 0; cnt < (unsigned int)num_iters;) {
    vector<Vec3d> verts_last = verts;

    if (!planar_only) {
      vector<Vec3d> near_pts;
      if (!alternate_loop) {
        for (auto &edge : edges) {
          Vec3d P = geom.edge_nearpt(edge, Vec3d(0, 0, 0));
          near_pts.push_back(P);
          Vec3d offset = edge_factor * (P.len() - 1) * P;
          verts[edge[0]] -= offset;
          verts[edge[1]] -= offset;
        }
      }
      // RK - alternate form causes the near points to be applied in a 2nd loop
      // most often not needed unless the model is off balance
      else {
        for (auto &edge : edges) {
          Vec3d P = geom.edge_nearpt(edge, Vec3d(0, 0, 0));
          near_pts.push_back(P);
          // RK - these 4 lines cause the near points to be applied in a 2nd
          // loop
        }
        int p_cnt = 0;
        for (auto &edge : edges) {
          Vec3d P = near_pts[p_cnt++];
          Vec3d offset = edge_factor * (P.len() - 1) * P;
          verts[edge[0]] -= offset;
          verts[edge[1]] -= offset;
        }
      }
      /*
            // RK - revolving loop. didn't solve the imbalance problem
            else {
              for (unsigned int ee = cnt; ee < edges.size() + cnt; ee++) {
                int e = ee % edges.size();
                Vec3d P = geom.edge_nearpt(edges[e], Vec3d(0, 0, 0));
                near_pts.push_back(P);
                Vec3d offset = edge_factor * (P.len() - 1) * P;
                verts[edges[e][0]] -= offset;
                verts[edges[e][1]] -= offset;
              }
            }
      */

      // re-center for drift
      Vec3d cent_near_pts = centroid(near_pts);
      for (unsigned int i = 0; i < verts.size(); i++)
        verts[i] -= cent_near_pts;
    }

    // Make a copy of verts into vs and zero out
    // Accumulate vertex changes instead of altering vertices in place
    // This can help relieve when a vertex is pushed towards one plane
    // and away from another
    vector<Vec3d> vs = verts;
    for (auto &v : vs)
      v = Vec3d(0, 0, 0);

    // progressively advances starting face each iteration
    for (unsigned int ff = cnt; ff < geom.faces().size() + cnt; ff++) {
      int f = ff % geom.faces().size();
      if (geom.faces(f).size() == 3)
        continue;
      Vec3d face_normal = face_normal_by_type(geom, f, normal_type).unit();
      Vec3d face_centroid = geom.face_cent(f);
      // make sure face_normal points outward
      if (vdot(face_normal, face_centroid) < 0)
        face_normal *= -1.0;
      // place a planar vertex over or under verts[v]
      // adds or subtracts it to get to the planar verts[v]
      for (int v : geom.faces(f))
        vs[v] += vdot(plane_factor * face_normal, face_centroid - verts[v]) *
                 face_normal;
    }

    // adjust vertices post-loop
    for (unsigned int i = 0; i < vs.size(); i++)
      verts[i] += vs[i];

    // len2() for difference value to minimize internal sqrt() calls
    max_diff2 = 0;
    for (unsigned int i = 0; i < verts.size(); i++) {
      double diff2 = (verts[i] - verts_last[i]).len2();
      if (diff2 > max_diff2)
        max_diff2 = diff2;
    }

    // increment count here for reporting
    cnt++;

    if ((rep_count > 0) && (cnt % rep_count == 0))
      fprintf(stderr, "%-15d max_diff=%.17g\n", cnt, sqrt(max_diff2));

    if (sqrt(max_diff2) < eps) {
      completed = true;
      break;
    }

    // if minimum and maximum radius are differing, the polyhedron is crumpling
    if (radius_range_percent &&
        canonical_radius_range_test(geom, radius_range_percent)) {
      fprintf(
          stderr,
          "\nbreaking out: radius range detected. try increasing percentage\n");
      break;
    }
  }

  if (rep_count > -1) {
    fprintf(stderr, "\n%-15d final max_diff=%.17g\n", cnt, sqrt(max_diff2));
    fprintf(stderr, "\n");
  }

  return completed;
}

// RK - wrapper for basic canonicalization with mathematical algorithm
// meant to be called with finite num_iters (not -1)
bool canonicalize_mm(Geometry &geom, const int num_iters, const int rep_count,
                     const double eps)
{
  char normal_type = 'n';
  bool alternate_loop = false;
  bool planarize_only = false;
  return canonicalize_mm(geom, 0.3, 0.5, num_iters, DBL_MAX, rep_count,
                         alternate_loop, planarize_only, normal_type, eps);
}

// RK - wrapper for basic planarization with mathematical algorithm
// meant to be called with finite num_iters (not -1)
bool planarize_mm(Geometry &geom, const int num_iters, const int rep_count,
                  const double eps)
{
  char normal_type = 'n';
  bool alternate_loop = false;
  bool planarize_only = true;
  return canonicalize_mm(geom, 0.3, 0.5, num_iters, DBL_MAX, rep_count,
                         alternate_loop, planarize_only, normal_type, eps);
}

// reciprocalN() is from the Hart's Conway Notation web page
// make array of vertices reciprocal to given planes (face normals)
// RK - save of verbatim port code
/*
vector<Vec3d> reciprocalN_old(const Geometry &geom)
{
  const vector<vector<int>> &faces = geom.faces();
  const vector<Vec3d> &verts = geom.verts();

  vector<Vec3d> normals;
  for (const auto &face : faces) {
    Vec3d centroid(0, 0, 0);
    Vec3d normal(0, 0, 0);
    double avgEdgeDist = 0;

    int v1 = face.at(face.size() - 2);
    int v2 = face.at(face.size() - 1);
    for (int v3 : face) {
      centroid += verts[v3];
      // orthogonal() was from the Hart's Conway Notation web page. replacement
      // normal += orthogonal(verts[v1], verts[v2], verts[v3]);
      normal += vcross(verts[v3] - verts[v2], verts[v2] - verts[v1]);
      // tangentPoint() was from Hart's Conway Notation web page. replacement
      // avgEdgeDist += tangentPoint(verts[v1], verts[v2]).len();
      Vec3d d = verts[v2] - verts[v1];
      // prevent division by zero
      // avgEdgeDist += (verts[v1] - ((vdot(d,verts[v1])/d.len2()) * d)).len();
      double vdt;
      if (d[0] == 0 && d[1] == 0 && d[2] == 0)
        vdt = 0;
      else
        vdt = vdot(d, verts[v1]) / d.len2();
      avgEdgeDist += (verts[v1] - (vdt * d)).len(); // tangentPoint without call
      v1 = v2;
      v2 = v3;
    }
    centroid *= 1.0 / face.size();
    normal.to_unit();
    avgEdgeDist /= face.size();

    // reciprocal call replace below:
    // prevent division by zero
    // Vec3d ans = reciprocal(normal * vdot(centroid,normal));
    Vec3d v = normal * vdot(centroid, normal);
    Vec3d ans;
    if (v[0] == 0 && v[1] == 0 && v[2] == 0)
      ans = v;
    else {
      ans = v * 1.0 / v.len2();
      ans *= (1 + avgEdgeDist) / 2;
    }
    normals.push_back(ans);
  }

  return normals;
}
*/

/* RK - save of simplified tangent code
      Vec3d d = verts[v2] - verts[v1];
      double vdt = 0;
      // prevent division by zero
      if (d[0] != 0 || d[1] != 0 || d[2] != 0)
        vdt = vdot(d, verts[v1]) / d.len2();
      avgEdgeDist += (verts[v1] - (vdt * d)).len();
*/

/*
// RK - Gives the same answer as the built in face_norm().unit()
// even when nonplanar and measuring all edges
Vec3d face_norm_newell(const Geometry &geom, vector<int> &face)
{
  const vector<Vec3d> &v = geom.verts();

  Vec3d face_normal(0, 0, 0);

  unsigned int sz = face.size();
  for (unsigned int i = 0; i < sz; i++) {
    int v1 = face[i];
    int v2 = face[(i + 1) % sz];

    face_normal[0] += (v[v1][1] - v[v2][1]) * (v[v1][2] + v[v2][2]);
    face_normal[1] += (v[v1][2] - v[v2][2]) * (v[v1][0] + v[v2][0]);
    face_normal[2] += (v[v1][0] - v[v2][0]) * (v[v1][1] + v[v2][1]);
  }

  return face_normal.to_unit();
}

// return the unit normal of all perimeter triangles
Vec3d face_norm_newell(const Geometry &geom, const int f_idx)
{
  vector<int> face = geom.faces(f_idx);
  return face_norm_newell(geom, face);
}
*/

// return the normal of all perimeter triangles
Vec3d face_norm_nonplanar_triangles(const Geometry &geom,
                                    const vector<int> &face)
{
  Vec3d face_normal(0, 0, 0);

  unsigned int sz = face.size();
  for (unsigned int i = 0; i < sz; i++) {
    int v0 = face[i];
    int v1 = face[(i + 1) % sz];
    int v2 = face[(i + 2) % sz];

    face_normal += vcross(geom.verts()[v0] - geom.verts()[v1],
                          geom.verts()[v1] - geom.verts()[v2]);
  }

  return face_normal;
}

// return the normal of all perimeter triangles
Vec3d face_norm_nonplanar_triangles(const Geometry &geom, const int f_idx)
{
  vector<int> face = geom.faces(f_idx);
  return face_norm_nonplanar_triangles(geom, face);
}

// return the normal of all quads in polygon
Vec3d face_norm_nonplanar_quads(const Geometry &geom, const vector<int> &face)
{
  Vec3d face_normal(0, 0, 0);

  unsigned int sz = face.size();
  for (unsigned int i = 0; i < sz; i++) {
    int v0 = face[i];
    int v1 = face[(i + 1) % sz];
    int v2 = face[(i + 2) % sz];
    int v3 = face[(i + 3) % sz];

    face_normal += vcross(geom.verts()[v0] - geom.verts()[v2],
                          geom.verts()[v1] - geom.verts()[v3]);
  }

  return face_normal;
}

// return the normal of quads in polygon
Vec3d face_norm_nonplanar_quads(const Geometry &geom, const int f_idx)
{
  vector<int> face = geom.faces(f_idx);
  return face_norm_nonplanar_quads(geom, face);
}

// select normal by type. Newell, triangles, or quads
// 'n' is the default, if normal_type is not given or is wrong
Vec3d face_normal_by_type(const Geometry &geom, const vector<int> &face,
                          const char normal_type)
{
  Vec3d face_normal;

  if (normal_type == 't')
    face_normal = face_norm_nonplanar_triangles(geom, face);
  else if (normal_type == 'q')
    face_normal = face_norm_nonplanar_quads(geom, face);
  else // if (normal_type == 'n')
    face_normal = geom.face_norm(face);
  return face_normal;
}

// select normal by type. Newell, triangles, or quads
Vec3d face_normal_by_type(const Geometry &geom, const int f_idx,
                          const char normal_type)
{
  vector<int> face = geom.faces(f_idx);
  return face_normal_by_type(geom, face, normal_type);
}

// reciprocalN() is from the Hart's Conway Notation web page
// make array of vertices reciprocal to given planes (face normals)
// RK - has accuracy issues and will have trouble with -l 16
vector<Vec3d> reciprocalN(const Geometry &geom, const char normal_type)
{
  vector<Vec3d> normals;

  for (const auto &face : geom.faces()) {
    // RK - the algoritm was written to use triangles for measuring
    // non-planar faces. Now method can be chosen
    Vec3d face_normal = face_normal_by_type(geom, face, normal_type).unit();
    Vec3d face_centroid = anti::centroid(geom.verts(), face);
    // make sure face_normal points outward
    if (vdot(face_normal, face_centroid) < 0)
      face_normal *= -1.0;

    // RK - find the average lenth of the edge near points
    unsigned int sz = face.size();
    double avgEdgeDist = 0;
    for (unsigned int j = 0; j < sz; j++) {
      int v1 = face[j];
      int v2 = face[(j + 1) % sz];

      avgEdgeDist += geom.edge_nearpt(make_edge(v1, v2), Vec3d(0, 0, 0)).len2();
    }

    // RK - sqrt of length squared here
    avgEdgeDist = sqrt(avgEdgeDist / sz);

    // the face normal height set to intersect face at v
    Vec3d v = face_normal * vdot(face_centroid, face_normal);

    // adjust v to the reciprocal value
    Vec3d ans = v;
    // prevent division by zero
    if (v[0] != 0 || v[1] != 0 || v[2] != 0)
      ans = v * 1.0 / v.len2();

    // edge correction (of v based on all edges of the face)
    ans *= (1 + avgEdgeDist) / 2;

    normals.push_back(ans);
  }

  return normals;
}

// reciprocate on face centers dividing by magnitude squared
vector<Vec3d> reciprocalC_len2(const Geometry &geom)
{
  vector<Vec3d> centers;
  geom.face_cents(centers);
  for (auto &center : centers)
    center /= center.len2();
  return centers;
}

// reciprocate on face centers dividing by magnitude
vector<Vec3d> reciprocalC_len(const Geometry &geom)
{
  vector<Vec3d> centers;
  geom.face_cents(centers);
  for (auto &center : centers)
    center /= center.len();
  return centers;
}

// Addition to algorithm by Adrian Rossiter
// Finds the edge near points centroid
Vec3d edge_nearpoints_centroid(Geometry &geom, const Vec3d cent)
{
  vector<vector<int>> edges;
  geom.get_impl_edges(edges);
  int e_sz = edges.size();
  Vec3d e_cent(0, 0, 0);
  for (auto &edge : edges)
    e_cent += geom.edge_nearpt(edge, cent);
  return e_cent / double(e_sz);
}

// Implementation of George Hart's planarization and canonicalization algorithms
// http://www.georgehart.com/virtual-polyhedra/conway_notation.html
bool canonicalize_bd(Geometry &base, const int num_iters,
                     const char canonical_method,
                     const double radius_range_percent, const int rep_count,
                     const char centering, const char normal_type,
                     const double eps)
{
  bool completed = false;

  Geometry dual;
  // the dual's initial vertex locations are immediately overwritten
  get_dual(dual, base, 1);
  dual.clear_cols();

  double max_diff2 = 0;
  unsigned int cnt;
  for (cnt = 0; cnt < (unsigned int)num_iters;) {
    vector<Vec3d> base_verts_last = base.verts();

    switch (canonical_method) {
    // base/dual canonicalize method
    case 'b': {
      dual.raw_verts() = reciprocalN(base, normal_type);
      base.raw_verts() = reciprocalN(dual, normal_type);
      if (centering != 'x') {
        Vec3d e_cent = edge_nearpoints_centroid(base, Vec3d(0, 0, 0));
        base.transform(Trans3d::translate(-0.1 * e_cent));
      }
      break;
    }

    // adjust vertices with side effect of planarization. len2() version
    case 'p':
      // move centroid to origin for balance
      dual.raw_verts() = reciprocalC_len2(base);
      base.transform(Trans3d::translate(-centroid(dual.verts())));
      base.raw_verts() = reciprocalC_len2(dual);
      base.transform(Trans3d::translate(-centroid(base.verts())));
      break;

    // adjust vertices with side effect of planarization. len() version
    case 'q':
      // move centroid to origin for balance
      dual.raw_verts() = reciprocalC_len(base);
      base.transform(Trans3d::translate(-centroid(dual.verts())));
      base.raw_verts() = reciprocalC_len(dual);
      base.transform(Trans3d::translate(-centroid(base.verts())));
      break;

    // adjust vertices with side effect of planarization. face centroids version
    case 'f':
      base.face_cents(dual.raw_verts());
      dual.face_cents(base.raw_verts());
      break;
    }

    // len2() for difference value to minimize internal sqrt() calls
    max_diff2 = 0;
    for (unsigned int i = 0; i < base.verts().size(); i++) {
      double diff2 = (base.verts(i) - base_verts_last[i]).len2();
      if (diff2 > max_diff2)
        max_diff2 = diff2;
    }

    // increment count here for reporting
    cnt++;

    if ((rep_count > 0) && (cnt % rep_count == 0))
      fprintf(stderr, "%-15d max_diff=%.17g\n", cnt, sqrt(max_diff2));

    if (sqrt(max_diff2) < eps) {
      completed = true;
      break;
    }

    // if minimum and maximum radius are differing, the polyhedron is crumpling
    if (radius_range_percent &&
        canonical_radius_range_test(base, radius_range_percent)) {
      fprintf(
          stderr,
          "\nbreaking out: radius range detected. try increasing percentage\n");
      break;
    }
  }

  if (rep_count > -1) {
    fprintf(stderr, "\n%-15d final max_diff=%.17g\n", cnt, sqrt(max_diff2));
    fprintf(stderr, "\n");
  }

  return completed;
}

// RK - wrapper for basic canonicalization with base/dual algorithm
// meant to be called with finite num_iters (not -1)
bool canonicalize_bd(Geometry &geom, const int num_iters, const int rep_count,
                     const double eps)
{
  char centering = 'x';
  char normal_type = 'n';
  return canonicalize_bd(geom, num_iters, 'b', DBL_MAX, rep_count, centering,
                         normal_type, eps);
}

// RK - wrapper for basic planarization with base/dual algorithm
// meant to be called with finite num_iters (not -1)
bool planarize_bd(Geometry &geom, const int num_iters, const int rep_count,
                  const double eps)
{
  char centering = 'x';
  char normal_type = 'n';
  return canonicalize_bd(geom, num_iters, 'p', DBL_MAX, rep_count, centering,
                         normal_type, eps);
}

// port for minmax unit (-a u) used for planarization
// parameters and code format changed to match above functions
// algorithm not changed
// used in canonical and conway
bool minmax_unit_planar(Geometry &geom, const double shorten_factor,
                        const double plane_factor, const double radius_factor,
                        const int num_iters, const double radius_range_percent,
                        const int rep_count, const char normal_type,
                        const double eps)
{
  bool completed = false;

  // do a scale to get edges close to 1
  GeometryInfo info(geom);
  double scale = info.iedge_length_lims().sum / info.num_iedges();
  if (scale)
    geom.transform(Trans3d::scale(1 / scale));

  const vector<Vec3d> &verts = geom.verts();
  const vector<vector<int>> &faces = geom.faces();

  Vec3d origin(0, 0, 0);
  vector<double> rads(faces.size());
  for (unsigned int f = 0; f < faces.size(); f++) {
    int N = faces[f].size();
    int D = abs(find_polygon_denominator_signed(geom, f, epsilon));
    if (!D)
      D = 1;
    rads[f] = 0.5 / sin(M_PI * D / N); // circumradius of regular polygon
    // fprintf(stderr, "{%d/%d} rad=%g\n", N, D, rads[f]);
  }

  double max_diff2 = 0;
  unsigned int cnt = 0;
  for (cnt = 0; cnt < (unsigned int)num_iters;) {
    vector<Vec3d> old_verts = verts;

    // Vertx offsets for the iteration.
    vector<Vec3d> offsets(verts.size(), Vec3d::zero);
    for (unsigned int ff = cnt; ff < faces.size() + cnt; ff++) {
      const unsigned int f = ff % faces.size();
      const vector<int> &face = faces[f];
      const unsigned int f_sz = face.size();
      // Vec3d norm = geom.face_norm(f).unit();
      Vec3d norm = face_normal_by_type(geom, f, normal_type).unit();
      Vec3d f_cent = geom.face_cent(f);
      if (vdot(norm, f_cent) < 0)
        norm *= -1.0;

      for (unsigned int vv = cnt; vv < f_sz + cnt; vv++) {
        unsigned int v = vv % f_sz;
        // offset for unit edges
        vector<int> edge = make_edge(face[v], face[(v + 1) % f_sz]);
        Vec3d offset =
            (1 - geom.edge_len(edge)) * shorten_factor * geom.edge_vec(edge);
        offsets[edge[0]] -= offset;
        offsets[edge[1]] += offset;

        // offset for planarity
        offsets[face[v]] +=
            vdot(plane_factor * norm, f_cent - verts[face[v]]) * norm;

        // offset for polygon radius
        Vec3d rad_vec = (verts[face[v]] - f_cent);
        offsets[face[v]] += (rads[f] - rad_vec.len()) * radius_factor * rad_vec;
      }
    }

    // adjust vertices post-loop
    for (unsigned int i = 0; i < offsets.size(); i++)
      geom.raw_verts()[i] += offsets[i];

    max_diff2 = 0;
    for (auto &offset : offsets) {
      double diff2 = offset.len2();
      if (diff2 > max_diff2)
        max_diff2 = diff2;
    }

    // increment count here for reporting
    cnt++;

    if ((rep_count > -1) && (cnt % rep_count == 0))
      fprintf(stderr, "%-15d max_diff=%.17g\n", cnt, sqrt(max_diff2));

    double width = BoundBox(verts).max_width();
    if (sqrt(max_diff2) / width < eps) {
      completed = true;
      break;
    }

    // if minimum and maximum radius are differing, the polyhedron is crumpling
    if (radius_range_percent &&
        canonical_radius_range_test(geom, radius_range_percent)) {
      fprintf(
          stderr,
          "\nbreaking out: radius range detected. try increasing percentage\n");
      break;
    }
  }

  if (rep_count > -1) {
    fprintf(stderr, "\n%-15d final max_diff=%.17g\n", cnt, sqrt(max_diff2));
    fprintf(stderr, "\n");
  }

  return completed;
}

// RK - wrapper for basic planarization with minmax -a u algorithm
// meant to be called with finite num_iters (not -1)
bool minmax_unit_planar(Geometry &geom, const int num_iters,
                        const int rep_count, const double eps)
{
  char normal_type = 'n';
  return (minmax_unit_planar(geom, 1.0 / 200, 1.0 / 200, 1.0 / 200, num_iters,
                             DBL_MAX, rep_count, normal_type, eps));
}

// RK - wrapper for basic planarization with minmax -a u algorithm
// copy, controls radius_range_percent, normal_type
bool minmax_unit_planar(Geometry &geom, const int num_iters,
                        const double radius_range_percent, const int rep_count,
                        const char normal_type, const double eps)
{
  return (minmax_unit_planar(geom, 1.0 / 200, 1.0 / 200, 1.0 / 200, num_iters,
                             radius_range_percent, rep_count, normal_type,
                             eps));
}

} // namespace anti
