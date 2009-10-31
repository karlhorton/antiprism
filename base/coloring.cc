/*
   Copyright (c) 2003-2008, Adrian Rossiter

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

/* \file coloring.cc
 * \brief classes to color all elements of a type.
 */

#include <algorithm>
#include "rand_gen.h"
#include "prop_col.h"
#include "coloring.h"
#include "transforms.h"
#include "bbox.h"
#include "utils.h"
#include "math_utils.h"
#include "info.h"

coloring::coloring(col_geom_v *geo): geom(geo), cycle_msecs(0)
{
}

coloring::~coloring()
{
   for(unsigned int i=0; i<cmaps.size(); i++)
      delete cmaps[i];
}

coloring::coloring(const coloring &clrng):
   geom(clrng.geom), cycle_msecs(clrng.cycle_msecs)
{
   for(unsigned int i=0; i<clrng.cmaps.size(); i++)
      add_cmap(clrng.cmaps[i]->clone());
}

coloring &coloring::operator=(const coloring &clrng)
{
   if(this!=&clrng) {
      geom = clrng.geom;
      cycle_msecs = clrng.cycle_msecs;
      while(cmaps.size())
         del_cmap();
      for(unsigned int i=0; i<clrng.cmaps.size(); i++)
         add_cmap(clrng.cmaps[i]->clone());
   }

   return *this;
}


void coloring::add_cmap(color_map *col_map, unsigned int pos)
{
   vector<color_map *>::iterator mi;
   if(pos>=cmaps.size())
      mi = cmaps.end();
   else
      mi = cmaps.begin()+pos;
   cmaps.insert(mi, col_map);
}


void coloring::del_cmap(unsigned int pos)
{
   if(cmaps.size()) {
      vector<color_map *>::iterator mi;
      if(pos>=cmaps.size())
         mi = cmaps.end()-1;
      else
         mi = cmaps.begin()+pos;
      delete *mi;
      cmaps.erase(mi);
   }
}


   
void coloring::cycle_map_cols()
{
   //cmap.cycle_colors();
}
       
col_val coloring::idx_to_val(int idx) const
{
   col_val col;
   for(unsigned int i=0; i<cmaps.size(); i++) {
      col = cmaps[i]->get_col(idx);
      if(col.is_set())
         break;
   }

   return col.is_set() ? col : col_val(idx);
}

 
void coloring::set_all_idx_to_val(map<int, col_val> &cols)
{
   map<int, col_val>::iterator mi;
   for(mi=cols.begin(); mi!=cols.end(); mi++)
      if(mi->second.is_idx())
         mi->second = idx_to_val(mi->second.get_idx());
}


inline double fract(double rng[], double frac)
{
   return fmod(rng[0] + (rng[1]-rng[0])*frac, 1+epsilon);
}


int coloring::y_gradient(vec3d vec, vec3d cent, double height, int def_sz)
{
   int sz = def_sz;
   if(cmaps.size()>0 && cmaps[0]->max_index()>0)
      sz = cmaps[0]->max_index();
   return (int)(sz * (0.5*height+(vec-cent)[1])/height);
}


void coloring::setup_lights(col_geom_v &lts)
{
   if(lts.verts().size()==0) {
      lts.add_col_vert(vec3d(1,0,0), vec3d(1,0,0));
      lts.add_col_vert(vec3d(0,1,0), vec3d(0,1,0));
      lts.add_col_vert(vec3d(0,0,1), vec3d(0,0,1));
      lts.add_col_vert(vec3d(-1,0,0), vec3d(0,1,1));
      lts.add_col_vert(vec3d(0,-1,0), vec3d(1,0,1));
      lts.add_col_vert(vec3d(0,0,-1), vec3d(1,1,0));
   }
   else
      for(unsigned int l=0; l<lts.verts().size(); l++)
         lts.raw_verts()[l].to_unit();
}



col_val coloring::light(vec3d vec, col_geom_v &lts)
{
   vec3d col_sum(0,0,0);
   vec.to_unit();
   double dot;
   for(unsigned int l=0; l<lts.verts().size(); l++)
      if((dot = vdot(vec, lts.verts(l)))>0)
         col_sum += dot * lts.get_v_col(l).get_vec3d();
   
   for(int j=0; j<3; j++)
      if(col_sum[j]>1)
         col_sum[j] = 1;

   return col_val(col_sum);
}





void coloring::v_apply_cmap()
{
   set_all_idx_to_val(get_geom()->raw_vert_cols());
}


void coloring::v_one_col(col_val col)
{
   for(unsigned int i=0; i<get_geom()->verts().size(); i++)
      get_geom()->set_v_col(i, col);
}


void coloring::v_unique(bool as_values)
{
   for(unsigned int i=0; i<get_geom()->verts().size(); i++) {
      if(as_values)
         get_geom()->set_v_col(i, idx_to_val(i));
      else
         get_geom()->set_v_col(i, i);
   }
}


void coloring::v_sets(const vector<set<int> > &equivs, bool as_values)
{
   for(unsigned int i=0; i<equivs.size(); i++) {
      for(set<int>::iterator si=equivs[i].begin(); si!=equivs[i].end(); ++si) {
         if(as_values)
            get_geom()->set_v_col(*si, idx_to_val(i));
         else
            get_geom()->set_v_col(*si, i);
      }
   }
}


void coloring::v_proper(bool as_values)
{
   long parameter[] = {1000, 10, 50, 5};
   long colours;
   Graph g(*get_geom());
   g.GraphColoring(parameter, colours);
   if(as_values)
      v_apply_cmap();
}


void coloring::v_order(bool as_values)
{
   vector<int> f_cnt(get_geom()->verts().size());
   for(unsigned int i=0; i<get_geom()->faces().size(); i++)
      for(unsigned int j=0; j<get_geom()->faces(i).size(); j++)
         f_cnt[get_geom()->faces(i, j)]++;
   for(unsigned int i=0; i<get_geom()->verts().size(); i++)
      if(as_values)
         get_geom()->set_v_col(i, idx_to_val(f_cnt[i]));
      else
         get_geom()->set_v_col(i, f_cnt[i]);
}


void coloring::v_position(bool as_values)
{
   bound_box bb(get_geom()->verts());
   vec3d cent = bb.get_centre();
   double height = bb.get_max()[1] - bb.get_min()[1];
   for(unsigned int i=0; i<get_geom()->verts().size(); i++) {
      int idx = y_gradient(get_geom()->verts(i), cent, height);
      if(as_values)
         get_geom()->set_v_col(i, idx_to_val(idx));
      else
         get_geom()->set_v_col(i, idx);
   }
}


void coloring::v_lights(col_geom_v lts)
{
   setup_lights(lts);
   vec3d cent = get_geom()->centroid();
   for(unsigned int i=0; i<get_geom()->verts().size(); i++)
      get_geom()->set_v_col(i, light(get_geom()->verts(i) - cent, lts));
}

void coloring::face_edge_color(const vector<vector<int> > &elems,
      const map<int, col_val> &cmap)
{
   vector<vector<int> > v_elems(get_geom()->verts().size());
   for(unsigned int i=0; i<elems.size(); ++i)
      for(unsigned int j=0; j<elems[i].size(); ++j)
         v_elems[elems[i][j]].push_back(i);
   
   for(unsigned int i=0; i<v_elems.size(); ++i) {
      vec4d col(0,0,0,0);
      int val_cnt = 0;
      int first_idx = -1;
      for(unsigned int j=0; j<v_elems[i].size(); ++j) {
         col_val ecol = col_geom::get_col(cmap, v_elems[i][j]);
         if(ecol.is_val()) {
            col += ecol.get_vec4d();
            val_cnt++;
         }
         else if(first_idx==-1 && ecol.is_idx())
            first_idx = ecol.get_idx();
      }
      if(val_cnt)
         get_geom()->set_v_col(i, col_val(col/double(val_cnt)));
      else if(first_idx!=-1)
         get_geom()->set_v_col(i, col_val(first_idx));
   }
}


void coloring::v_face_color()
{
   face_edge_color(get_geom()->faces(), get_geom()->face_cols());
}


void coloring::v_edge_color()
{
   face_edge_color(get_geom()->edges(), get_geom()->edge_cols());
}





void coloring::f_apply_cmap()
{
   set_all_idx_to_val(get_geom()->raw_face_cols());
}


void coloring::f_one_col(col_val col)
{
   for(unsigned int i=0; i<get_geom()->faces().size(); i++)
      get_geom()->set_f_col(i, col);
}


void coloring::f_sets(const vector<set<int> > &equivs, bool as_values)
{
   for(unsigned int i=0; i<equivs.size(); i++) {
      col_val col(i);
      for(set<int>::iterator si=equivs[i].begin(); si!=equivs[i].end(); ++si) {
         if(as_values)
            get_geom()->set_f_col(*si, idx_to_val(i));
         else
            get_geom()->set_f_col(*si, i);
      }
   }
}


void coloring::f_unique(bool as_values)
{
   for(unsigned int i=0; i<get_geom()->faces().size(); i++) {
      if(as_values)
         get_geom()->set_f_col(i, idx_to_val(i));
      else
         get_geom()->set_f_col(i, i);
   }
}


void coloring::f_proper(bool as_values)
{
   // set up duall graph
   geom_v dual;
   get_dual(*get_geom(), dual);
   col_geom_v dgeom(dual);
   long parameter[] = {1000, 10, 50, 5};
   long colours;
   Graph g(dgeom);
   g.GraphColoring(parameter, colours);
   for(unsigned int i=0; i<get_geom()->faces().size(); i++)
      if(as_values)
         get_geom()->set_f_col(i,idx_to_val(dgeom.get_v_col(i).get_idx()));
      else
         get_geom()->set_f_col(i, dgeom.get_v_col(i).get_idx());
}


void coloring::f_sides(bool as_values)
{
   for(unsigned int i=0; i<get_geom()->faces().size(); i++)
      if(as_values)
         get_geom()->set_f_col(i, idx_to_val(get_geom()->faces(i).size()));
      else
         get_geom()->set_f_col(i, get_geom()->faces(i).size());
}


void coloring::f_avg_angle(bool as_values)
{
   geom_info info(*get_geom());
   int faces_sz = get_geom()->faces().size();
   for(int i=0; i<faces_sz; i++) {
      vector<double> f_angs;
      info.face_angles_lengths(i, f_angs);
      double ang_sum = 0;
      for(unsigned int j=0; j<f_angs.size(); j++)
         ang_sum += f_angs[j];
      int idx = (int)(rad2deg(ang_sum/f_angs.size())+0.5);
      if(as_values)
         get_geom()->set_f_col(i, idx_to_val(idx));
      else
         get_geom()->set_f_col(i, idx);
   }
}


void coloring::f_parts(bool as_values)
{
   vector<vector<int> > parts;
   geom_v gtmp = *get_geom();
   gtmp.orient(&parts);
   for(unsigned int i=0; i<parts.size(); i++)
      for(unsigned int j=0; j<parts[i].size(); j++)
         if(as_values)
            get_geom()->set_f_col(parts[i][j], idx_to_val(i));
         else
            get_geom()->set_f_col(parts[i][j], i);
}

void coloring::f_normal(bool as_values)
{
   for(unsigned int i=0; i<get_geom()->faces().size(); i++) {
      int idx = y_gradient(get_geom()->face_norm(i).unit());
      if(as_values)
         get_geom()->set_f_col(i, idx_to_val(idx));
      else
         get_geom()->set_f_col(i, idx);
   }
}


void coloring::f_centroid(bool as_values)
{
   bound_box bb(get_geom()->verts());
   vec3d cent = bb.get_centre();
   double height = bb.get_max()[1] - bb.get_min()[1];
   for(unsigned int i=0; i<get_geom()->faces().size(); i++) {
      int idx = y_gradient(get_geom()->face_cent(i), cent, height);
      if(as_values)
         get_geom()->set_f_col(i, idx_to_val(idx));
      else
         get_geom()->set_f_col(i, idx);
   }
}

void coloring::f_lights(col_geom_v lts)
{
   setup_lights(lts);
   for(unsigned int i=0; i<get_geom()->faces().size(); i++)
      get_geom()->set_f_col(i, light(get_geom()->face_norm(i), lts));
}

void coloring::f_lights2(col_geom_v lts)
{
   setup_lights(lts);
   for(unsigned int i=0; i<get_geom()->faces().size(); i++)
      get_geom()->set_f_col(i, light(get_geom()->face_cent(i), lts));
   
}




void coloring::e_apply_cmap()
{ 
   set_all_idx_to_val(get_geom()->raw_edge_cols());
}

void coloring::e_one_col(col_val col)
{
   for(unsigned int i=0; i<get_geom()->edges().size(); i++)
      get_geom()->set_e_col(i, col);
}


void coloring::e_sets(const vector<set<int> > &equivs, bool as_values)
{
   for(unsigned int i=0; i<equivs.size(); i++) {
      for(set<int>::iterator si=equivs[i].begin(); si!=equivs[i].end(); ++si) {
         if(as_values)
            get_geom()->set_e_col(*si, idx_to_val(i));
         else
            get_geom()->set_e_col(*si, i);
      }
   }
}


void coloring::e_unique(bool as_values)
{
   for(unsigned int i=0; i<get_geom()->edges().size(); i++) {
      if(as_values)
         get_geom()->set_e_col(i, idx_to_val(i));
      else
         get_geom()->set_e_col(i, i);
   }
}


void coloring::e_proper(bool as_values)
{
   // set up edges to faces graph
   col_geom_v egeom;
   edges_to_faces(*get_geom(), egeom, true);
   col_geom_v dgeom;
   get_dual(egeom, dgeom);
   long parameter[] = {1000, 10, 50, 5};
   long colours;
   Graph g(dgeom);
   g.GraphColoring(parameter, colours);
   const vector<vector<int> > &faces = egeom.faces();
   for(unsigned int i=0; i<faces.size(); i++) {
      col_val col(0,0,0);
      if(as_values)
         col = idx_to_val(dgeom.get_v_col(i).get_idx());
      else
         col = dgeom.get_v_col(i).get_idx();
      get_geom()->add_col_edge(faces[i][0], faces[i][2], col);
   }
}
 


void coloring::e_face_color()
{
   const vector<vector<int> > &faces = get_geom()->faces();
   vector<vector<int> > efaces(get_geom()->edges().size());
   for(unsigned int i=0; i<faces.size(); ++i) {
      for(unsigned int j=0; j<faces[i].size(); ++j) {
         vector<int> edge =
            make_edge(faces[i][j],faces[i][(j+1)%faces[i].size()]);
         vector<vector<int> >::const_iterator ei = get_geom()->edges().begin();
         while((ei=find(ei, get_geom()->edges().end(), edge)) !=
               get_geom()->edges().end()) {
            efaces[ei-get_geom()->edges().begin()].push_back(i);
            ++ei;
         }
      }
   }
   
   for(unsigned int i=0; i<efaces.size(); ++i) {
      vec4d col(0,0,0,0);
      int val_cnt = 0;
      int first_idx = -1;
      for(unsigned int j=0; j<efaces[i].size(); ++j) {
         col_val fcol = get_geom()->get_f_col(efaces[i][j]);
         if(fcol.is_val()) {
            col += fcol.get_vec4d();
            val_cnt++;
         }
         else if(first_idx==-1 && fcol.is_idx())
            first_idx = fcol.get_idx();
      }
      if(val_cnt)
         get_geom()->set_e_col(i, col_val(col/double(val_cnt)));
      else if(first_idx!=-1)
         get_geom()->set_e_col(i, col_val(first_idx));
   }
}


void coloring::edge_color_and_branch(int idx, int part, bool as_values,
      vector<vector<int> > &vcons, vector<bool> &seen)
{
   if(seen[idx])
      return;
   else
      seen[idx] = true;

   for(unsigned int i=0; i<vcons[idx].size(); i++) {
      int next_idx = vcons[idx][i];
      if(idx == next_idx)
         continue;
      vector<int> edge(2);
      edge[0] = idx;
      edge[1] = next_idx;
      int e_idx = get_geom()->add_edge(edge);
      if(as_values)
         get_geom()->set_e_col(e_idx, idx_to_val(part));
      else
         get_geom()->set_e_col(e_idx, part);
      edge_color_and_branch(next_idx, part, as_values, vcons, seen);
   }
}



void coloring::e_parts(bool as_values)
{
   vector<vector<int> > vcons(get_geom()->get_verts()->size(), vector<int>());
   const vector<vector<int> > &edges = get_geom()->edges();
   for(unsigned int i=0; i<edges.size(); i++) {
      vcons[edges[i][0]].push_back(edges[i][1]);
      vcons[edges[i][1]].push_back(edges[i][0]);
   }

   int part=0;
   vector<bool> seen(vcons.size(), false);
   for(unsigned int i=0; i<vcons.size(); i++)
      if(!seen[i])
         edge_color_and_branch(i, part++, as_values, vcons, seen);
}


void coloring::e_direction(bool as_values)
{
   for(unsigned int i=0; i<get_geom()->edges().size(); i++) {
      vec3d v = -2.0*(get_geom()->edge_vec(i)).unit();
      if(v[1]<0)
         v = -v;
      int idx = y_gradient(v);
      if(as_values)
         get_geom()->set_e_col(i, idx_to_val(idx));
      else
         get_geom()->set_e_col(i, idx);
   }
}

void coloring::e_mid_point(bool as_values)
{
   bound_box bb(get_geom()->verts());
   vec3d cent = bb.get_centre();
   double height = bb.get_max()[1] - bb.get_min()[1];
   for(unsigned int i=0; i<get_geom()->edges().size(); i++) {
      int idx = y_gradient(get_geom()->edge_cent(i).unit(), cent, height);
      if(as_values)
         get_geom()->set_e_col(i, idx_to_val(idx));
      else
         get_geom()->set_e_col(i, idx);
   }
}



void coloring::e_lights(col_geom_v lts)
{
   setup_lights(lts);
   vec3d cent = get_geom()->centroid();
   for(unsigned int i=0; i<get_geom()->edges().size(); i++)
      get_geom()->set_e_col(i, light(get_geom()->edge_nearpt(i,cent)-cent,lts));
}


static bool get_cycle_rate(const char *str, double *cps)
{
   size_t len = strlen(str);
   if(len>3 && str[len-2]=='h' && str[len-1]=='z') {
      char str_copy[MSG_SZ];
      strncpy(str_copy, str, len-2);
      double cycs;
      if(read_double(str_copy, &cycs) && cycs>=0.0) {
         *cps = cycs;
         return true;
      }
   }

   return false;
}




bool read_colorings(coloring clrngs[], const char *line, char *errmsg,
      int max_parts)
{
   if(errmsg)
      *errmsg = '\0';

   char line_copy[MSG_SZ];
   strncpy(line_copy, line, MSG_SZ);
   line_copy[MSG_SZ-1] = '\0';

   vector<char *> parts;
   int parts_sz = split_line(line_copy, parts, ",");
   if(parts_sz>max_parts) {
      if(errmsg)
         sprintf(errmsg, "the argument has more than %d part(s)", max_parts);
      return 0;
   }
   
   /*
   string cfile = parts[0];
   if(cfile=="") {
      if(errmsg)
         strcpy(errmsg, "colour map file name not given");
      return 0;
   }
   */

   char errmsg2[MSG_SZ];
   vector<char *> map_names;
   coloring clrng;
   unsigned int conv_elems = 7;
   
   for(int i=0; i<parts_sz; i++) {
      color_map *col_map = init_color_map(parts[i], errmsg2);
      double cps;
      if(get_cycle_rate(parts[i], &cps)) {
         clrng.set_cycle_msecs((int)(1000/cps));
         if(col_map && errmsg)
            snprintf(errmsg, MSG_SZ,
                  "cycle_rate '%s' is also a valid colour map name", parts[i]);
      }
      else if(strspn(parts[i], "vef") == strlen(parts[i])) {
         conv_elems = 4*(strchr(parts[i], 'f')!=0) +
                      2*(strchr(parts[i], 'e')!=0) +
                      1*(strchr(parts[i], 'v')!=0);
         if(col_map && errmsg)
            snprintf(errmsg, MSG_SZ,
                  "conversion elements '%s' is also a valid colour map name",
                  parts[i]);
      }
      else if(col_map)
         clrng.add_cmap(col_map);
      else {
         if(errmsg)
            strcpy(errmsg, errmsg2);
         return 0;
      }
   }

   for(int i=0; i<3; i++) {
      if((conv_elems & (1<<i)))
         clrngs[i] = clrng;
   }

   return 1;
}

 
