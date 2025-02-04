// this file is split into many separate cgalutils* files
// in order to workaround gcc 4.9.1 crashing on systems with only 2GB of RAM

#ifdef ENABLE_CGAL

#include "cgal.h"
#include "cgalutils.h"
#include "Feature.h"
#include "PolySet.h"
#include "printutils.h"
#include "progress.h"
#include "CGALHybridPolyhedron.h"
#ifdef ENABLE_MANIFOLD
#include "ManifoldGeometry.h"
#include "manifoldutils.h"
#endif
#include "node.h"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/normal_vector_newell_3.h>
#include <CGAL/Handle_hash_function.h>

#include <CGAL/config.h>
#include <CGAL/version.h>

#include <CGAL/convex_hull_3.h>

#include "memory.h"
#include "Reindexer.h"
#include "GeometryUtils.h"

#include <map>
#include <queue>
#include <unordered_set>

namespace CGALUtils {

shared_ptr<const Geometry> applyUnion3D(
Geometry::Geometries::iterator chbegin, Geometry::Geometries::iterator chend)
{
  using QueueConstItem = std::pair<shared_ptr<const CGAL_Nef_polyhedron>, int>;
  struct QueueItemGreater {
    // stable sort for priority_queue by facets, then progress mark
    bool operator()(const QueueConstItem& lhs, const QueueConstItem& rhs) const
    {
      size_t l = lhs.first->p3->number_of_facets();
      size_t r = rhs.first->p3->number_of_facets();
      return (l > r) || (l == r && lhs.second > rhs.second);
    }
  };
  std::priority_queue<QueueConstItem, std::vector<QueueConstItem>, QueueItemGreater> q;

  try {
    // sort children by fewest faces
    for (auto it = chbegin; it != chend; ++it) {
      auto curChild = getNefPolyhedronFromGeometry(it->second);
      if (curChild && !curChild->isEmpty()) {
        int node_mark = -1;
        if (it->first) {
          node_mark = it->first->progress_mark;
        }
        q.emplace(curChild, node_mark);
      }
    }

    progress_tick();
    while (q.size() > 1) {
      auto p1 = q.top();
      q.pop();
      auto p2 = q.top();
      q.pop();
      q.emplace(make_shared<const CGAL_Nef_polyhedron>(*p1.first + *p2.first), -1);
      progress_tick();
    }

    if (q.size() == 1) {
      return shared_ptr<const Geometry>(new CGAL_Nef_polyhedron(q.top().first->p3));
    } else {
      return nullptr;
    }
  } catch (const CGAL::Failure_exception& e) {
    LOG(message_group::Error, "CGAL error in CGALUtils::applyUnion3D: %1$s", e.what());
  }
  return nullptr;
}

/*!
   Applies op to all children and returns the result.
   The child list should be guaranteed to contain non-NULL 3D or empty Geometry objects
 */
shared_ptr<const Geometry> applyOperator3D(const Geometry::Geometries& children, OpenSCADOperator op)
{
  CGAL_Nef_polyhedron *N = nullptr;

  assert(op != OpenSCADOperator::UNION && "use applyUnion3D() instead of applyOperator3D()");
  bool foundFirst = false;

  try {
    for (const auto& item : children) {
      const shared_ptr<const Geometry>& chgeom = item.second;
      auto chN = getNefPolyhedronFromGeometry(chgeom);

      // Initialize N with first expected geometric object
      if (!foundFirst) {
        if (chN) {
          N = new CGAL_Nef_polyhedron(*chN);
        } else { // first child geometry might be empty/null
          N = nullptr;
        }
        foundFirst = true;
        continue;
      }

      // Intersecting something with nothing results in nothing
      if (!chN || chN->isEmpty()) {
        if (op == OpenSCADOperator::INTERSECTION) {
          if (N != nullptr) delete N; // safety!
          N = nullptr;
        }
        continue;
      }

      // empty op <something> => empty
      if (!N || N->isEmpty()) continue;

      switch (op) {
      case OpenSCADOperator::INTERSECTION:
        *N *= *chN;
        break;
      case OpenSCADOperator::DIFFERENCE:
        *N -= *chN;
        break;
      case OpenSCADOperator::MINKOWSKI:
        N->minkowski(*chN);
        break;
      default:
        LOG(message_group::Error, "Unsupported CGAL operator: %1$d", static_cast<int>(op));
      }
      if (item.first) item.first->progress_report();
    }
  }
  // union && difference assert triggered by tests/data/scad/bugs/rotate-diff-nonmanifold-crash.scad and tests/data/scad/bugs/issue204.scad
  catch (const CGAL::Failure_exception& e) {
    std::string opstr = op == OpenSCADOperator::INTERSECTION ? "intersection" : op == OpenSCADOperator::DIFFERENCE ? "difference" : op == OpenSCADOperator::UNION ? "union" : "UNKNOWN";
    LOG(message_group::Error, "CGAL error in CGALUtils::applyBinaryOperator %1$s: %2$s", opstr, e.what());
  }
  // boost any_cast throws exceptions inside CGAL code, ending here https://github.com/openscad/openscad/issues/3756
  catch (const std::exception& e) {
    std::string opstr = op == OpenSCADOperator::INTERSECTION ? "intersection" : op == OpenSCADOperator::DIFFERENCE ? "difference" : op == OpenSCADOperator::UNION ? "union" : "UNKNOWN";
    LOG(message_group::Error, "exception in CGALUtils::applyBinaryOperator %1$s: %2$s", opstr, e.what());
  }
  return shared_ptr<Geometry>(N);
}

}  // namespace CGALUtils


#endif // ENABLE_CGAL








