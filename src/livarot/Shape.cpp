/*
 *  Shape.cpp
 *  nlivarot
 *
 *  Created by fred on Thu Jun 12 2003.
 *
 */

#include <glib/gmem.h>
#include "Shape.h"
//#include "MyMath.h"
#include <libnr/nr-point.h>
#include <libnr/nr-point-fns.h>

/*
 * Shape instances handling.
 * never (i repeat: never) modify edges and points links; use Connect() and Disconnect() instead
 * the graph is stored as a set of points and edges, with edges in a doubly-linked list for each point.
 */

Shape::Shape (void)
  : _need_points_sorting(false),
    _need_edges_sorting(false),
    _has_points_data(false),
    _has_edges_data(false),
    _has_sweep_src_data(false),
    _has_sweep_dest_data(false),
    _has_sweep_data(false),
    _has_raster_data(false),
    _has_quick_raster_data(false),
    _has_back_data(false),
    _has_voronoi_data(false)
{
  leftX = topY = rightX = bottomY = 0;
  maxPt = 0;
  maxAr = 0;

  type = shape_polygon;
}
Shape::~Shape (void)
{
  maxPt = 0;
  maxAr = 0;
}

void Shape::Affiche(void)
{
  /*
  printf("sh=%p nbPt=%i nbAr=%i\n",this,nbPt,nbAr); // localizing ok
  for (int i=0;i<nbPt;i++) {
    printf("pt %i : x=(%f %f) dI=%i dO=%i\n",i,pts[i].x[0],pts[i].x[1],pts[i].dI,pts[i].dO); // localizing ok
  }
  for (int i=0;i<nbAr;i++) {
    printf("ar %i : dx=(%f %f) st=%i en=%i\n",i,aretes[i].dx[0],aretes[i].dx[1],aretes[i].st,aretes[i].en); // localizing ok
  }
  */
}

void
Shape::MakePointData (bool nVal)
{
  if (nVal)
    {
      if (_has_points_data == false)
	{
	  _has_points_data = true;
	  pData.resize(maxPt);
	}
    }
  else
    {
      if (_has_points_data)
	{
	  _has_points_data = false;
	  pData.clear();
	}
    }
}
void
Shape::MakeEdgeData (bool nVal)
{
  if (nVal)
    {
      if (_has_edges_data == false)
	{
	  _has_edges_data = true;
	  eData.resize(maxAr);
	}
    }
  else
    {
      if (_has_edges_data)
	{
	  _has_edges_data = false;
	  eData.clear();
	}
    }
}
void
Shape::MakeRasterData (bool nVal)
{
  if (nVal)
    {
      if (_has_raster_data == false)
	{
	  _has_raster_data = true;
	  swrData.resize(maxAr);
	}
    }
  else
    {
      if (_has_raster_data)
	{
	  _has_raster_data = false;
	  swrData.clear();
	}
    }
}
void
Shape::MakeQuickRasterData (bool nVal)
{
  if (nVal)
    {
      if (_has_quick_raster_data == false)
	{
	  _has_quick_raster_data = true;
	  qrsData.resize(maxAr);
	}
    }
  else
    {
      if (_has_quick_raster_data)
	{
	  _has_quick_raster_data = false;
	  qrsData.clear();
	}
    }
}
void
Shape::MakeSweepSrcData (bool nVal)
{
  if (nVal)
    {
      if (_has_sweep_src_data == false)
	{
	  _has_sweep_src_data = true;
	  swsData.resize(maxAr);
	}
    }
  else
    {
      if (_has_sweep_src_data)
	{
	  _has_sweep_src_data = false;
	  swsData.clear();
	}
    }
}
void
Shape::MakeSweepDestData (bool nVal)
{
  if (nVal)
    {
      if (_has_sweep_dest_data == false)
	{
	  _has_sweep_dest_data = true;
	  swdData.resize(maxAr);
	}
    }
  else
    {
      if (_has_sweep_dest_data)
	{
	  _has_sweep_dest_data = false;
	  swdData.clear();
	}
    }
}
void
Shape::MakeBackData (bool nVal)
{
  if (nVal)
    {
      if (_has_back_data == false)
	{
	  _has_back_data = true;
	  ebData.resize(maxAr);
	}
    }
  else
    {
      if (_has_back_data)
	{
	  _has_back_data = false;
	  ebData.clear();
	}
    }
}
void
Shape::MakeVoronoiData (bool nVal)
{
  if (nVal)
    {
      if (_has_voronoi_data == false)
	{
	  _has_voronoi_data = true;
	  vorpData.resize(maxPt);
	  voreData.resize(maxAr);
	}
    }
  else
    {
      if (_has_voronoi_data)
	{
	  _has_voronoi_data = false;
	  vorpData.clear();
	  voreData.clear();
	}
    }
}


/**
 *  Copy point and edge data from `who' into this object, discarding
 *  any cached data that we have.
 */
void
Shape::Copy (Shape * who)
{
  if (who == NULL)
    {
      Reset (0, 0);
      return;
    }
  MakePointData (false);
  MakeEdgeData (false);
  MakeSweepSrcData (false);
  MakeSweepDestData (false);
  MakeRasterData (false);
  MakeQuickRasterData (false);
  MakeBackData (false);
  if (_has_sweep_data)
    {
      SweepTree::DestroyList (sTree);
      SweepEvent::DestroyQueue (sEvts);
      _has_sweep_data = false;
    }

  Reset (who->numberOfPoints(), who->numberOfEdges());
  type = who->type;
  _need_points_sorting = who->_need_points_sorting;
  _need_edges_sorting = who->_need_edges_sorting;
  _has_points_data = false;
  _has_edges_data = false;
  _has_sweep_src_data = false;
  _has_sweep_dest_data = false;
  _has_sweep_data = false;
  _has_raster_data = false;
  _has_quick_raster_data = false;
  _has_back_data = false;
  _has_voronoi_data = false;

  _pts = who->_pts;
  _aretes = who->_aretes;
}

void
Shape::Reset (int n, int m)
{
  _pts.clear();
  _aretes.clear();
  
  type = shape_polygon;
  if (n > maxPt)
    {
      maxPt = n;
      if (_has_points_data)
	pData.resize(maxPt);
      if (_has_voronoi_data)
	vorpData.resize(maxPt);
    }
  if (m > maxAr)
    {
      maxAr = m;
      if (_has_edges_data)
	eData.resize(maxAr);
      if (_has_sweep_dest_data)
	swdData.resize(maxAr);
      if (_has_sweep_src_data)
	swsData.resize(maxAr);
      if (_has_back_data)
	ebData.resize(maxAr);
      if (_has_voronoi_data)
	voreData.resize(maxAr);
    }
  _need_points_sorting = false;
  _need_edges_sorting = false;
}

int
Shape::AddPoint (const NR::Point x)
{
  if (numberOfPoints() >= maxPt)
    {
      maxPt = 2 * numberOfPoints() + 1;
      if (_has_points_data)
	pData.resize(maxPt);
      if (_has_voronoi_data)
	vorpData.resize(maxPt);
    }

  dg_point p;
  p.x = x;
  p.dI = p.dO = 0;
  p.firstA = p.lastA = -1;
  p.oldDegree = -1;
  _pts.push_back(p);
  int const n = _pts.size() - 1;

  if (_has_points_data)
    {
      pData[n].pending = 0;
      pData[n].edgeOnLeft = -1;
      pData[n].nextLinkedPoint = -1;
      pData[n].askForWindingS = NULL;
      pData[n].askForWindingB = -1;
    }
  if (_has_voronoi_data)
    {
      vorpData[n].value = 0.0;
      vorpData[n].winding = -2;
    }
  _need_points_sorting = true;

  return n;
}

void
Shape::SubPoint (int p)
{
  if (p < 0 || p >= numberOfPoints())
    return;
  _need_points_sorting = true;
  int cb;
  cb = getPoint(p).firstA;
  while (cb >= 0 && cb < numberOfEdges())
    {
      if (getEdge(cb).st == p)
	{
	  int ncb = getEdge(cb).nextS;
	  _aretes[cb].nextS = _aretes[cb].prevS = -1;
	  _aretes[cb].st = -1;
	  cb = ncb;
	}
      else if (getEdge(cb).en == p)
	{
	  int ncb = getEdge(cb).nextE;
	  _aretes[cb].nextE = _aretes[cb].prevE = -1;
	  _aretes[cb].en = -1;
	  cb = ncb;
	}
      else
	{
	  break;
	}
    }
  _pts[p].firstA = _pts[p].lastA = -1;
  if (p < numberOfPoints() - 1)
    SwapPoints (p, numberOfPoints() - 1);
  _pts.pop_back();
}

void
Shape::SwapPoints (int a, int b)
{
  if (a == b)
    return;
  if (getPoint(a).totalDegree() == 2 && getPoint(b).totalDegree() == 2)
    {
      int cb = getPoint(a).firstA;
      if (getEdge(cb).st == a)
	{
	  _aretes[cb].st = numberOfPoints();
	}
      else if (getEdge(cb).en == a)
	{
	  _aretes[cb].en = numberOfPoints();
	}
      cb = getPoint(a).lastA;
      if (getEdge(cb).st == a)
	{
	  _aretes[cb].st = numberOfPoints();
	}
      else if (getEdge(cb).en == a)
	{
	  _aretes[cb].en = numberOfPoints();
	}

      cb = getPoint(b).firstA;
      if (getEdge(cb).st == b)
	{
	  _aretes[cb].st = a;
	}
      else if (getEdge(cb).en == b)
	{
	  _aretes[cb].en = a;
	}
      cb = getPoint(b).lastA;
      if (getEdge(cb).st == b)
	{
	  _aretes[cb].st = a;
	}
      else if (getEdge(cb).en == b)
	{
	  _aretes[cb].en = a;
	}

      cb = getPoint(a).firstA;
      if (getEdge(cb).st == numberOfPoints())
	{
	  _aretes[cb].st = b;
	}
      else if (getEdge(cb).en == numberOfPoints())
	{
	  _aretes[cb].en = b;
	}
      cb = getPoint(a).lastA;
      if (getEdge(cb).st == numberOfPoints())
	{
	  _aretes[cb].st = b;
	}
      else if (getEdge(cb).en == numberOfPoints())
	{
	  _aretes[cb].en = b;
	}

    }
  else
    {
      int cb;
      cb = getPoint(a).firstA;
      while (cb >= 0)
	{
	  int ncb = NextAt (a, cb);
	  if (getEdge(cb).st == a)
	    {
	      _aretes[cb].st = numberOfPoints();
	    }
	  else if (getEdge(cb).en == a)
	    {
	      _aretes[cb].en = numberOfPoints();
	    }
	  cb = ncb;
	}
      cb = getPoint(b).firstA;
      while (cb >= 0)
	{
	  int ncb = NextAt (b, cb);
	  if (getEdge(cb).st == b)
	    {
	      _aretes[cb].st = a;
	    }
	  else if (getEdge(cb).en == b)
	    {
	      _aretes[cb].en = a;
	    }
	  cb = ncb;
	}
      cb = getPoint(a).firstA;
      while (cb >= 0)
	{
	  int ncb = NextAt (numberOfPoints(), cb);
	  if (getEdge(cb).st == numberOfPoints())
	    {
	      _aretes[cb].st = b;
	    }
	  else if (getEdge(cb).en == numberOfPoints())
	    {
	      _aretes[cb].en = b;
	    }
	  cb = ncb;
	}
    }
  {
    dg_point swap = getPoint(a);
    _pts[a] = getPoint(b);
    _pts[b] = swap;
  }
  if (_has_points_data)
    {
      point_data swad = pData[a];
      pData[a] = pData[b];
      pData[b] = swad;
      //              pData[pData[a].oldInd].newInd=a;
      //              pData[pData[b].oldInd].newInd=b;
    }
  if (_has_voronoi_data)
    {
      voronoi_point swav = vorpData[a];
      vorpData[a] = vorpData[b];
      vorpData[b] = swav;
    }
}
void
Shape::SwapPoints (int a, int b, int c)
{
  if (a == b || b == c || a == c)
    return;
  SwapPoints (a, b);
  SwapPoints (b, c);
}

void
Shape::SortPoints (void)
{
  if (_need_points_sorting && hasPoints())
    SortPoints (0, numberOfPoints() - 1);
  _need_points_sorting = false;
}

void
Shape::SortPointsRounded (void)
{
  if (hasPoints())
    SortPointsRounded (0, numberOfPoints() - 1);
}

void
Shape::SortPoints (int s, int e)
{
  if (s >= e)
    return;
  if (e == s + 1)
    {
      if (getPoint(s).x[1] > getPoint(e).x[1]
	  || (getPoint(s).x[1] == getPoint(e).x[1] && getPoint(s).x[0] > getPoint(e).x[0]))
	SwapPoints (s, e);
      return;
    }

  int ppos = (s + e) / 2;
  int plast = ppos;
  double pvalx = getPoint(ppos).x[0];
  double pvaly = getPoint(ppos).x[1];

  int le = s, ri = e;
  while (le < ppos || ri > plast)
    {
      if (le < ppos)
	{
	  do
	    {
	      int test = 0;
	      if (getPoint(le).x[1] > pvaly)
		{
		  test = 1;
		}
	      else if (getPoint(le).x[1] == pvaly)
		{
		  if (getPoint(le).x[0] > pvalx)
		    {
		      test = 1;
		    }
		  else if (getPoint(le).x[0] == pvalx)
		    {
		      test = 0;
		    }
		  else
		    {
		      test = -1;
		    }
		}
	      else
		{
		  test = -1;
		}
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (le < ppos - 1)
		    {
		      SwapPoints (le, ppos - 1, ppos);
		      ppos--;
		      continue;	// sans changer le
		    }
		  else if (le == ppos - 1)
		    {
		      ppos--;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test > 0)
		{
		  break;
		}
	      le++;
	    }
	  while (le < ppos);
	}
      if (ri > plast)
	{
	  do
	    {
	      int test = 0;
	      if (getPoint(ri).x[1] > pvaly)
		{
		  test = 1;
		}
	      else if (getPoint(ri).x[1] == pvaly)
		{
		  if (getPoint(ri).x[0] > pvalx)
		    {
		      test = 1;
		    }
		  else if (getPoint(ri).x[0] == pvalx)
		    {
		      test = 0;
		    }
		  else
		    {
		      test = -1;
		    }
		}
	      else
		{
		  test = -1;
		}
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (ri > plast + 1)
		    {
		      SwapPoints (ri, plast + 1, plast);
		      plast++;
		      continue;	// sans changer ri
		    }
		  else if (ri == plast + 1)
		    {
		      plast++;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test < 0)
		{
		  break;
		}
	      ri--;
	    }
	  while (ri > plast);
	}
      if (le < ppos)
	{
	  if (ri > plast)
	    {
	      SwapPoints (le, ri);
	      le++;
	      ri--;
	    }
	  else
	    {
	      if (le < ppos - 1)
		{
		  SwapPoints (ppos - 1, plast, le);
		  ppos--;
		  plast--;
		}
	      else if (le == ppos - 1)
		{
		  SwapPoints (plast, le);
		  ppos--;
		  plast--;
		}
	    }
	}
      else
	{
	  if (ri > plast + 1)
	    {
	      SwapPoints (plast + 1, ppos, ri);
	      ppos++;
	      plast++;
	    }
	  else if (ri == plast + 1)
	    {
	      SwapPoints (ppos, ri);
	      ppos++;
	      plast++;
	    }
	  else
	    {
	      break;
	    }
	}
    }
  SortPoints (s, ppos - 1);
  SortPoints (plast + 1, e);
}

void
Shape::SortPointsByOldInd (int s, int e)
{
  if (s >= e)
    return;
  if (e == s + 1)
    {
      if (getPoint(s).x[1] > getPoint(e).x[1] || (getPoint(s).x[1] == getPoint(e).x[1] && getPoint(s).x[0] > getPoint(e).x[0])
	  || (getPoint(s).x[1] == getPoint(e).x[1] && getPoint(s).x[0] == getPoint(e).x[0]
	      && pData[s].oldInd > pData[e].oldInd))
	SwapPoints (s, e);
      return;
    }

  int ppos = (s + e) / 2;
  int plast = ppos;
  double pvalx = getPoint(ppos).x[0];
  double pvaly = getPoint(ppos).x[1];
  int pvali = pData[ppos].oldInd;

  int le = s, ri = e;
  while (le < ppos || ri > plast)
    {
      if (le < ppos)
	{
	  do
	    {
	      int test = 0;
	      if (getPoint(le).x[1] > pvaly)
		{
		  test = 1;
		}
	      else if (getPoint(le).x[1] == pvaly)
		{
		  if (getPoint(le).x[0] > pvalx)
		    {
		      test = 1;
		    }
		  else if (getPoint(le).x[0] == pvalx)
		    {
		      if (pData[le].oldInd > pvali)
			{
			  test = 1;
			}
		      else if (pData[le].oldInd == pvali)
			{
			  test = 0;
			}
		      else
			{
			  test = -1;
			}
		    }
		  else
		    {
		      test = -1;
		    }
		}
	      else
		{
		  test = -1;
		}
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (le < ppos - 1)
		    {
		      SwapPoints (le, ppos - 1, ppos);
		      ppos--;
		      continue;	// sans changer le
		    }
		  else if (le == ppos - 1)
		    {
		      ppos--;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test > 0)
		{
		  break;
		}
	      le++;
	    }
	  while (le < ppos);
	}
      if (ri > plast)
	{
	  do
	    {
	      int test = 0;
	      if (getPoint(ri).x[1] > pvaly)
		{
		  test = 1;
		}
	      else if (getPoint(ri).x[1] == pvaly)
		{
		  if (getPoint(ri).x[0] > pvalx)
		    {
		      test = 1;
		    }
		  else if (getPoint(ri).x[0] == pvalx)
		    {
		      if (pData[ri].oldInd > pvali)
			{
			  test = 1;
			}
		      else if (pData[ri].oldInd == pvali)
			{
			  test = 0;
			}
		      else
			{
			  test = -1;
			}
		    }
		  else
		    {
		      test = -1;
		    }
		}
	      else
		{
		  test = -1;
		}
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (ri > plast + 1)
		    {
		      SwapPoints (ri, plast + 1, plast);
		      plast++;
		      continue;	// sans changer ri
		    }
		  else if (ri == plast + 1)
		    {
		      plast++;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test < 0)
		{
		  break;
		}
	      ri--;
	    }
	  while (ri > plast);
	}
      if (le < ppos)
	{
	  if (ri > plast)
	    {
	      SwapPoints (le, ri);
	      le++;
	      ri--;
	    }
	  else
	    {
	      if (le < ppos - 1)
		{
		  SwapPoints (ppos - 1, plast, le);
		  ppos--;
		  plast--;
		}
	      else if (le == ppos - 1)
		{
		  SwapPoints (plast, le);
		  ppos--;
		  plast--;
		}
	    }
	}
      else
	{
	  if (ri > plast + 1)
	    {
	      SwapPoints (plast + 1, ppos, ri);
	      ppos++;
	      plast++;
	    }
	  else if (ri == plast + 1)
	    {
	      SwapPoints (ppos, ri);
	      ppos++;
	      plast++;
	    }
	  else
	    {
	      break;
	    }
	}
    }
  SortPointsByOldInd (s, ppos - 1);
  SortPointsByOldInd (plast + 1, e);
}

void
Shape::SortPointsRounded (int s, int e)
{
  if (s >= e)
    return;
  if (e == s + 1)
    {
      if (pData[s].rx[1] > pData[e].rx[1]
	  || (pData[s].rx[1] == pData[e].rx[1] && pData[s].rx[0] > pData[e].rx[0]))
	SwapPoints (s, e);
      return;
    }

  int ppos = (s + e) / 2;
  int plast = ppos;
  double pvalx = pData[ppos].rx[0];
  double pvaly = pData[ppos].rx[1];

  int le = s, ri = e;
  while (le < ppos || ri > plast)
    {
      if (le < ppos)
	{
	  do
	    {
	      int test = 0;
	      if (pData[le].rx[1] > pvaly)
		{
		  test = 1;
		}
	      else if (pData[le].rx[1] == pvaly)
		{
		  if (pData[le].rx[0] > pvalx)
		    {
		      test = 1;
		    }
		  else if (pData[le].rx[0] == pvalx)
		    {
		      test = 0;
		    }
		  else
		    {
		      test = -1;
		    }
		}
	      else
		{
		  test = -1;
		}
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (le < ppos - 1)
		    {
		      SwapPoints (le, ppos - 1, ppos);
		      ppos--;
		      continue;	// sans changer le
		    }
		  else if (le == ppos - 1)
		    {
		      ppos--;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test > 0)
		{
		  break;
		}
	      le++;
	    }
	  while (le < ppos);
	}
      if (ri > plast)
	{
	  do
	    {
	      int test = 0;
	      if (pData[ri].rx[1] > pvaly)
		{
		  test = 1;
		}
	      else if (pData[ri].rx[1] == pvaly)
		{
		  if (pData[ri].rx[0] > pvalx)
		    {
		      test = 1;
		    }
		  else if (pData[ri].rx[0] == pvalx)
		    {
		      test = 0;
		    }
		  else
		    {
		      test = -1;
		    }
		}
	      else
		{
		  test = -1;
		}
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (ri > plast + 1)
		    {
		      SwapPoints (ri, plast + 1, plast);
		      plast++;
		      continue;	// sans changer ri
		    }
		  else if (ri == plast + 1)
		    {
		      plast++;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test < 0)
		{
		  break;
		}
	      ri--;
	    }
	  while (ri > plast);
	}
      if (le < ppos)
	{
	  if (ri > plast)
	    {
	      SwapPoints (le, ri);
	      le++;
	      ri--;
	    }
	  else
	    {
	      if (le < ppos - 1)
		{
		  SwapPoints (ppos - 1, plast, le);
		  ppos--;
		  plast--;
		}
	      else if (le == ppos - 1)
		{
		  SwapPoints (plast, le);
		  ppos--;
		  plast--;
		}
	    }
	}
      else
	{
	  if (ri > plast + 1)
	    {
	      SwapPoints (plast + 1, ppos, ri);
	      ppos++;
	      plast++;
	    }
	  else if (ri == plast + 1)
	    {
	      SwapPoints (ppos, ri);
	      ppos++;
	      plast++;
	    }
	  else
	    {
	      break;
	    }
	}
    }
  SortPointsRounded (s, ppos - 1);
  SortPointsRounded (plast + 1, e);
}

/*
 *
 */
int
Shape::AddEdge (int st, int en)
{
  if (st == en)
    return -1;
  if (st < 0 || en < 0)
    return -1;
  type = shape_graph;
  if (numberOfEdges() >= maxAr)
    {
      maxAr = 2 * numberOfEdges() + 1;
      if (_has_edges_data)
	eData.resize(maxAr);
      if (_has_sweep_src_data)
	swsData.resize(maxAr);
      if (_has_sweep_dest_data)
	swdData.resize(maxAr);
      if (_has_raster_data)
	swrData.resize(maxAr);
      if (_has_back_data)
	ebData.resize(maxAr);
      if (_has_voronoi_data)
	voreData.resize(maxAr);
    }

  dg_arete a;
  a.dx = NR::Point(0, 0);
  a.st = a.en = -1;
  a.prevS = a.nextS = -1;
  a.prevE = a.nextE = -1;
  if (st >= 0 && en >= 0) {
    a.dx = getPoint(en).x - getPoint(st).x;
  }

  _aretes.push_back(a);
  int const n = numberOfEdges() - 1;
  
  ConnectStart (st, n);
  ConnectEnd (en, n);
  if (_has_edges_data)
    {
      eData[n].weight = 1;
      eData[n].rdx = getEdge(n).dx;
    }
  if (_has_sweep_src_data)
    {
      swsData[n].misc = NULL;
      swsData[n].firstLinkedPoint = -1;
    }
  if (_has_back_data)
    {
      ebData[n].pathID = -1;
      ebData[n].pieceID = -1;
      ebData[n].tSt = ebData[n].tEn = 0;
    }
  if (_has_voronoi_data)
    {
      voreData[n].leF = -1;
      voreData[n].riF = -1;
    }
  _need_edges_sorting = true;
  return n;
}

int
Shape::AddEdge (int st, int en, int leF, int riF)
{
  if (st == en)
    return -1;
  if (st < 0 || en < 0)
    return -1;
  {
    int cb = getPoint(st).firstA;
    while (cb >= 0)
      {
	if (getEdge(cb).st == st && getEdge(cb).en == en)
	  return -1;		// doublon
	if (getEdge(cb).st == en && getEdge(cb).en == st)
	  return -1;		// doublon
	cb = NextAt (st, cb);
      }
  }
  type = shape_graph;
  if (numberOfEdges() >= maxAr)
    {
      maxAr = 2 * numberOfEdges() + 1;
      if (_has_edges_data)
	eData.resize(maxAr);
      if (_has_sweep_src_data)
	swsData.resize(maxAr);
      if (_has_sweep_dest_data)
	swdData.resize(maxAr);
      if (_has_raster_data)
	swrData.resize(maxAr);
      if (_has_back_data)
	ebData.resize(maxAr);
      if (_has_voronoi_data)
	voreData.resize(maxAr);
    }

  dg_arete a;
  a.dx = NR::Point(0, 0);
  a.st = a.en = -1;
  a.prevS = a.nextS = -1;
  a.prevE = a.nextE = -1;
  if (st >= 0 && en >= 0) {
    a.dx = getPoint(en).x - getPoint(st).x;
  }
  
  _aretes.push_back(a);
  int const n = numberOfEdges() - 1;

  ConnectStart (st, n);
  ConnectEnd (en, n);
  if (_has_edges_data)
    {
      eData[n].weight = 1;
      eData[n].rdx = getEdge(n).dx;
    }
  if (_has_sweep_src_data)
    {
      swsData[n].misc = NULL;
      swsData[n].firstLinkedPoint = -1;
    }
  if (_has_back_data)
    {
      ebData[n].pathID = -1;
      ebData[n].pieceID = -1;
      ebData[n].tSt = ebData[n].tEn = 0;
    }
  if (_has_voronoi_data)
    {
      voreData[n].leF = leF;
      voreData[n].riF = riF;
    }
  _need_edges_sorting = true;
  return n;
}

void
Shape::SubEdge (int e)
{
  if (e < 0 || e >= numberOfEdges())
    return;
  type = shape_graph;
  DisconnectStart (e);
  DisconnectEnd (e);
  if (e < numberOfEdges() - 1)
    SwapEdges (e, numberOfEdges() - 1);
  _aretes.pop_back();
  _need_edges_sorting = true;
}

void
Shape::SwapEdges (int a, int b)
{
  if (a == b)
    return;
  if (getEdge(a).prevS >= 0 && getEdge(a).prevS != b)
    {
      if (getEdge(getEdge(a).prevS).st == getEdge(a).st)
	{
	  _aretes[getEdge(a).prevS].nextS = b;
	}
      else if (getEdge(getEdge(a).prevS).en == getEdge(a).st)
	{
	  _aretes[getEdge(a).prevS].nextE = b;
	}
    }
  if (getEdge(a).nextS >= 0 && getEdge(a).nextS != b)
    {
      if (getEdge(getEdge(a).nextS).st == getEdge(a).st)
	{
	  _aretes[getEdge(a).nextS].prevS = b;
	}
      else if (getEdge(getEdge(a).nextS).en == getEdge(a).st)
	{
	  _aretes[getEdge(a).nextS].prevE = b;
	}
    }
  if (getEdge(a).prevE >= 0 && getEdge(a).prevE != b)
    {
      if (getEdge(getEdge(a).prevE).st == getEdge(a).en)
	{
	  _aretes[getEdge(a).prevE].nextS = b;
	}
      else if (getEdge(getEdge(a).prevE).en == getEdge(a).en)
	{
	  _aretes[getEdge(a).prevE].nextE = b;
	}
    }
  if (getEdge(a).nextE >= 0 && getEdge(a).nextE != b)
    {
      if (getEdge(getEdge(a).nextE).st == getEdge(a).en)
	{
	  _aretes[getEdge(a).nextE].prevS = b;
	}
      else if (getEdge(getEdge(a).nextE).en == getEdge(a).en)
	{
	  _aretes[getEdge(a).nextE].prevE = b;
	}
    }
  if (getEdge(a).st >= 0)
    {
      if (getPoint(getEdge(a).st).firstA == a)
	_pts[getEdge(a).st].firstA = numberOfEdges();
      if (getPoint(getEdge(a).st).lastA == a)
	_pts[getEdge(a).st].lastA = numberOfEdges();
    }
  if (getEdge(a).en >= 0)
    {
      if (getPoint(getEdge(a).en).firstA == a)
	_pts[getEdge(a).en].firstA = numberOfEdges();
      if (getPoint(getEdge(a).en).lastA == a)
	_pts[getEdge(a).en].lastA = numberOfEdges();
    }


  if (getEdge(b).prevS >= 0 && getEdge(b).prevS != a)
    {
      if (getEdge(getEdge(b).prevS).st == getEdge(b).st)
	{
	  _aretes[getEdge(b).prevS].nextS = a;
	}
      else if (getEdge(getEdge(b).prevS).en == getEdge(b).st)
	{
	  _aretes[getEdge(b).prevS].nextE = a;
	}
    }
  if (getEdge(b).nextS >= 0 && getEdge(b).nextS != a)
    {
      if (getEdge(getEdge(b).nextS).st == getEdge(b).st)
	{
	  _aretes[getEdge(b).nextS].prevS = a;
	}
      else if (getEdge(getEdge(b).nextS).en == getEdge(b).st)
	{
	  _aretes[getEdge(b).nextS].prevE = a;
	}
    }
  if (getEdge(b).prevE >= 0 && getEdge(b).prevE != a)
    {
      if (getEdge(getEdge(b).prevE).st == getEdge(b).en)
	{
	  _aretes[getEdge(b).prevE].nextS = a;
	}
      else if (getEdge(getEdge(b).prevE).en == getEdge(b).en)
	{
	  _aretes[getEdge(b).prevE].nextE = a;
	}
    }
  if (getEdge(b).nextE >= 0 && getEdge(b).nextE != a)
    {
      if (getEdge(getEdge(b).nextE).st == getEdge(b).en)
	{
	  _aretes[getEdge(b).nextE].prevS = a;
	}
      else if (getEdge(getEdge(b).nextE).en == getEdge(b).en)
	{
	  _aretes[getEdge(b).nextE].prevE = a;
	}
    }
  if (getEdge(b).st >= 0)
    {
      if (getPoint(getEdge(b).st).firstA == b)
	_pts[getEdge(b).st].firstA = a;
      if (getPoint(getEdge(b).st).lastA == b)
	_pts[getEdge(b).st].lastA = a;
    }
  if (getEdge(b).en >= 0)
    {
      if (getPoint(getEdge(b).en).firstA == b)
	_pts[getEdge(b).en].firstA = a;
      if (getPoint(getEdge(b).en).lastA == b)
	_pts[getEdge(b).en].lastA = a;
    }

  if (getEdge(a).st >= 0)
    {
      if (getPoint(getEdge(a).st).firstA == numberOfEdges())
	_pts[getEdge(a).st].firstA = b;
      if (getPoint(getEdge(a).st).lastA == numberOfEdges())
	_pts[getEdge(a).st].lastA = b;
    }
  if (getEdge(a).en >= 0)
    {
      if (getPoint(getEdge(a).en).firstA == numberOfEdges())
	_pts[getEdge(a).en].firstA = b;
      if (getPoint(getEdge(a).en).lastA == numberOfEdges())
	_pts[getEdge(a).en].lastA = b;
    }

  if (getEdge(a).prevS == b)
    _aretes[a].prevS = a;
  if (getEdge(a).prevE == b)
    _aretes[a].prevE = a;
  if (getEdge(a).nextS == b)
    _aretes[a].nextS = a;
  if (getEdge(a).nextE == b)
    _aretes[a].nextE = a;
  if (getEdge(b).prevS == a)
    _aretes[a].prevS = b;
  if (getEdge(b).prevE == a)
    _aretes[a].prevE = b;
  if (getEdge(b).nextS == a)
    _aretes[a].nextS = b;
  if (getEdge(b).nextE == a)
    _aretes[a].nextE = b;

  dg_arete swap = getEdge(a);
  _aretes[a] = getEdge(b);
  _aretes[b] = swap;
  if (_has_edges_data)
    {
      edge_data swae = eData[a];
      eData[a] = eData[b];
      eData[b] = swae;
    }
  if (_has_sweep_src_data)
    {
      sweep_src_data swae = swsData[a];
      swsData[a] = swsData[b];
      swsData[b] = swae;
    }
  if (_has_sweep_dest_data)
    {
      sweep_dest_data swae = swdData[a];
      swdData[a] = swdData[b];
      swdData[b] = swae;
    }
  if (_has_raster_data)
    {
      raster_data swae = swrData[a];
      swrData[a] = swrData[b];
      swrData[b] = swae;
    }
  if (_has_back_data)
    {
      back_data swae = ebData[a];
      ebData[a] = ebData[b];
      ebData[b] = swae;
    }
  if (_has_voronoi_data)
    {
      voronoi_edge swav = voreData[a];
      voreData[a] = voreData[b];
      voreData[b] = swav;
    }
}
void
Shape::SwapEdges (int a, int b, int c)
{
  if (a == b || b == c || a == c)
    return;
  SwapEdges (a, b);
  SwapEdges (b, c);
}

void
Shape::SortEdges (void)
{
  if (_need_edges_sorting == false) {
    return;
  }
  _need_edges_sorting = false;

  edge_list *list = (edge_list *) g_malloc(numberOfEdges() * sizeof (edge_list));
  for (int p = 0; p < numberOfPoints(); p++)
    {
      int const d = getPoint(p).totalDegree();
      if (d > 1)
	{
	  int cb;
	  cb = getPoint(p).firstA;
	  int nb = 0;
	  while (cb >= 0)
	    {
	      int n = nb++;
	      list[n].no = cb;
	      if (getEdge(cb).st == p)
		{
		  list[n].x = getEdge(cb).dx;
		  list[n].starting = true;
		}
	      else
		{
		  list[n].x = -getEdge(cb).dx;
		  list[n].starting = false;
		}
	      cb = NextAt (p, cb);
	    }
	  SortEdgesList (list, 0, nb - 1);
	  _pts[p].firstA = list[0].no;
	  _pts[p].lastA = list[nb - 1].no;
	  for (int i = 0; i < nb; i++)
	    {
	      if (list[i].starting)
		{
		  if (i > 0)
		    {
		      _aretes[list[i].no].prevS = list[i - 1].no;
		    }
		  else
		    {
		      _aretes[list[i].no].prevS = -1;
		    }
		  if (i < nb - 1)
		    {
		      _aretes[list[i].no].nextS = list[i + 1].no;
		    }
		  else
		    {
		      _aretes[list[i].no].nextS = -1;
		    }
		}
	      else
		{
		  if (i > 0)
		    {
		      _aretes[list[i].no].prevE = list[i - 1].no;
		    }
		  else
		    {
		      _aretes[list[i].no].prevE = -1;
		    }
		  if (i < nb - 1)
		    {
		      _aretes[list[i].no].nextE = list[i + 1].no;
		    }
		  else
		    {
		      _aretes[list[i].no].nextE = -1;
		    }
		}
	    }
	}
    }
  g_free(list);
}

int
Shape::CmpToVert (NR::Point ax, NR::Point bx,bool as,bool bs)
{
  int tstAX = 0;
  int tstAY = 0;
  int tstBX = 0;
  int tstBY = 0;
  if (ax[0] > 0)
    tstAX = 1;
  if (ax[0] < 0)
    tstAX = -1;
  if (ax[1] > 0)
    tstAY = 1;
  if (ax[1] < 0)
    tstAY = -1;
  if (bx[0] > 0)
    tstBX = 1;
  if (bx[0] < 0)
    tstBX = -1;
  if (bx[1] > 0)
    tstBY = 1;
  if (bx[1] < 0)
    tstBY = -1;

  int quadA = 0, quadB = 0;
  if (tstAX < 0)
    {
      if (tstAY < 0)
	{
	  quadA = 7;
	}
      else if (tstAY == 0)
	{
	  quadA = 6;
	}
      else if (tstAY > 0)
	{
	  quadA = 5;
	}
    }
  else if (tstAX == 0)
    {
      if (tstAY < 0)
	{
	  quadA = 0;
	}
      else if (tstAY == 0)
	{
	  quadA = -1;
	}
      else if (tstAY > 0)
	{
	  quadA = 4;
	}
    }
  else if (tstAX > 0)
    {
      if (tstAY < 0)
	{
	  quadA = 1;
	}
      else if (tstAY == 0)
	{
	  quadA = 2;
	}
      else if (tstAY > 0)
	{
	  quadA = 3;
	}
    }
  if (tstBX < 0)
    {
      if (tstBY < 0)
	{
	  quadB = 7;
	}
      else if (tstBY == 0)
	{
	  quadB = 6;
	}
      else if (tstBY > 0)
	{
	  quadB = 5;
	}
    }
  else if (tstBX == 0)
    {
      if (tstBY < 0)
	{
	  quadB = 0;
	}
      else if (tstBY == 0)
	{
	  quadB = -1;
	}
      else if (tstBY > 0)
	{
	  quadB = 4;
	}
    }
  else if (tstBX > 0)
    {
      if (tstBY < 0)
	{
	  quadB = 1;
	}
      else if (tstBY == 0)
	{
	  quadB = 2;
	}
      else if (tstBY > 0)
	{
	  quadB = 3;
	}
    }
  if (quadA < quadB)
    return 1;
  if (quadA > quadB)
    return -1;

  NR::Point av, bv;
  av = ax;
  bv = bx;
  double si = cross (bv, av);
  int tstSi = 0;
  if (si > 0.000001) tstSi = 1;
  if (si < -0.000001) tstSi = -1;
  if ( tstSi == 0 ) {
    if ( as == true && bs == false ) return -1;
    if ( as == false && bs == true ) return 1;
  }
  return tstSi;
}

void
Shape::SortEdgesList (edge_list * list, int s, int e)
{
  if (s >= e)
    return;
  if (e == s + 1) {
    int cmpval=CmpToVert (list[e].x, list[s].x,list[e].starting,list[s].starting);
    if ( cmpval > 0 )  { // priorite aux sortants
      edge_list swap = list[s];
      list[s] = list[e];
      list[e] = swap;
    }
    return;
 }

  int ppos = (s + e) / 2;
  int plast = ppos;
  NR::Point pvalx = list[ppos].x;
  bool      pvals = list[ppos].starting;
  
  int le = s, ri = e;
  while (le < ppos || ri > plast)
    {
      if (le < ppos)
	{
	  do
	    {
        int test = CmpToVert (pvalx, list[le].x,pvals,list[le].starting);
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (le < ppos - 1)
		    {
		      edge_list swap = list[le];
		      list[le] = list[ppos - 1];
		      list[ppos - 1] = list[ppos];
		      list[ppos] = swap;
		      ppos--;
		      continue;	// sans changer le
		    }
		  else if (le == ppos - 1)
		    {
		      ppos--;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test > 0)
		{
		  break;
		}
	      le++;
	    }
	  while (le < ppos);
	}
      if (ri > plast)
	{
	  do
	    {
        int test = CmpToVert (pvalx, list[ri].x,pvals,list[ri].starting);
	      if (test == 0)
		{
		  // on colle les valeurs egales au pivot ensemble
		  if (ri > plast + 1)
		    {
		      edge_list swap = list[ri];
		      list[ri] = list[plast + 1];
		      list[plast + 1] = list[plast];
		      list[plast] = swap;
		      plast++;
		      continue;	// sans changer ri
		    }
		  else if (ri == plast + 1)
		    {
		      plast++;
		      break;
		    }
		  else
		    {
		      // oupsie
		      break;
		    }
		}
	      if (test < 0)
		{
		  break;
		}
	      ri--;
	    }
	  while (ri > plast);
	}

      if (le < ppos)
	{
	  if (ri > plast)
	    {
	      edge_list swap = list[le];
	      list[le] = list[ri];
	      list[ri] = swap;
	      le++;
	      ri--;
	    }
	  else if (le < ppos - 1)
	    {
	      edge_list swap = list[ppos - 1];
	      list[ppos - 1] = list[plast];
	      list[plast] = list[le];
	      list[le] = swap;
	      ppos--;
	      plast--;
	    }
	  else if (le == ppos - 1)
	    {
	      edge_list swap = list[plast];
	      list[plast] = list[le];
	      list[le] = swap;
	      ppos--;
	      plast--;
	    }
	  else
	    {
	      break;
	    }
	}
      else
	{
	  if (ri > plast + 1)
	    {
	      edge_list swap = list[plast + 1];
	      list[plast + 1] = list[ppos];
	      list[ppos] = list[ri];
	      list[ri] = swap;
	      ppos++;
	      plast++;
	    }
	  else if (ri == plast + 1)
	    {
	      edge_list swap = list[ppos];
	      list[ppos] = list[ri];
	      list[ri] = swap;
	      ppos++;
	      plast++;
	    }
	  else
	    {
	      break;
	    }
	}
    }
  SortEdgesList (list, s, ppos - 1);
  SortEdgesList (list, plast + 1, e);

}



/*
 *
 */
void
Shape::ConnectStart (int p, int b)
{
  if (getEdge(b).st >= 0)
    DisconnectStart (b);
  
  _aretes[b].st = p;
  _pts[p].dO++;
  _aretes[b].nextS = -1;
  _aretes[b].prevS = getPoint(p).lastA;
  if (getPoint(p).lastA >= 0)
    {
      if (getEdge(getPoint(p).lastA).st == p)
	{
	  _aretes[getPoint(p).lastA].nextS = b;
	}
      else if (getEdge(getPoint(p).lastA).en == p)
	{
	  _aretes[getPoint(p).lastA].nextE = b;
	}
    }
  _pts[p].lastA = b;
  if (getPoint(p).firstA < 0)
    _pts[p].firstA = b;
}

void
Shape::ConnectEnd (int p, int b)
{
  if (getEdge(b).en >= 0)
    DisconnectEnd (b);
  _aretes[b].en = p;
  _pts[p].dI++;
  _aretes[b].nextE = -1;
  _aretes[b].prevE = getPoint(p).lastA;
  if (getPoint(p).lastA >= 0)
    {
      if (getEdge(getPoint(p).lastA).st == p)
	{
	  _aretes[getPoint(p).lastA].nextS = b;
	}
      else if (getEdge(getPoint(p).lastA).en == p)
	{
	  _aretes[getPoint(p).lastA].nextE = b;
	}
    }
  _pts[p].lastA = b;
  if (getPoint(p).firstA < 0)
    _pts[p].firstA = b;
}

void
Shape::DisconnectStart (int b)
{
  if (getEdge(b).st < 0)
    return;
  _pts[getEdge(b).st].dO--;
  if (getEdge(b).prevS >= 0)
    {
      if (getEdge(getEdge(b).prevS).st == getEdge(b).st)
	{
	  _aretes[getEdge(b).prevS].nextS = getEdge(b).nextS;
	}
      else if (getEdge(getEdge(b).prevS).en == getEdge(b).st)
	{
	  _aretes[getEdge(b).prevS].nextE = getEdge(b).nextS;
	}
    }
  if (getEdge(b).nextS >= 0)
    {
      if (getEdge(getEdge(b).nextS).st == getEdge(b).st)
	{
	  _aretes[getEdge(b).nextS].prevS = getEdge(b).prevS;
	}
      else if (getEdge(getEdge(b).nextS).en == getEdge(b).st)
	{
	  _aretes[getEdge(b).nextS].prevE = getEdge(b).prevS;
	}
    }
  if (getPoint(getEdge(b).st).firstA == b)
    _pts[getEdge(b).st].firstA = getEdge(b).nextS;
  if (getPoint(getEdge(b).st).lastA == b)
    _pts[getEdge(b).st].lastA = getEdge(b).prevS;
  _aretes[b].st = -1;
}

void
Shape::DisconnectEnd (int b)
{
  if (getEdge(b).en < 0)
    return;
  _pts[getEdge(b).en].dI--;
  if (getEdge(b).prevE >= 0)
    {
      if (getEdge(getEdge(b).prevE).st == getEdge(b).en)
	{
	  _aretes[getEdge(b).prevE].nextS = getEdge(b).nextE;
	}
      else if (getEdge(getEdge(b).prevE).en == getEdge(b).en)
	{
	  _aretes[getEdge(b).prevE].nextE = getEdge(b).nextE;
	}
    }
  if (getEdge(b).nextE >= 0)
    {
      if (getEdge(getEdge(b).nextE).st == getEdge(b).en)
	{
	  _aretes[getEdge(b).nextE].prevS = getEdge(b).prevE;
	}
      else if (getEdge(getEdge(b).nextE).en == getEdge(b).en)
	{
	  _aretes[getEdge(b).nextE].prevE = getEdge(b).prevE;
	}
    }
  if (getPoint(getEdge(b).en).firstA == b)
    _pts[getEdge(b).en].firstA = getEdge(b).nextE;
  if (getPoint(getEdge(b).en).lastA == b)
    _pts[getEdge(b).en].lastA = getEdge(b).prevE;
  _aretes[b].en = -1;
}

bool
Shape::Eulerian (bool directed)
{
  if (directed)
    {
      for (int i = 0; i < numberOfPoints(); i++)
	{
	  if (getPoint(i).dI != getPoint(i).dO)
	    {
	      return false;
	    }
	}
    }
  else
    {
      for (int i = 0; i < numberOfPoints(); i++)
	{
	  if (getPoint(i).totalDegree() % 2 == 1)
	    {
	      return false;
	    }
	}
    }
  return true;
}

void
Shape::Inverse (int b)
{
  int swap;
  swap = getEdge(b).st;
  _aretes[b].st = getEdge(b).en;
  _aretes[b].en = swap;
  swap = getEdge(b).prevE;
  _aretes[b].prevE = getEdge(b).prevS;
  _aretes[b].prevS = swap;
  swap = getEdge(b).nextE;
  _aretes[b].nextE = getEdge(b).nextS;
  _aretes[b].nextS = swap;
  _aretes[b].dx = -getEdge(b).dx;
  if (getEdge(b).st >= 0)
    {
      _pts[getEdge(b).st].dO++;
      _pts[getEdge(b).st].dI--;
    }
  if (getEdge(b).en >= 0)
    {
      _pts[getEdge(b).en].dO--;
      _pts[getEdge(b).en].dI++;
    }
  if (_has_edges_data)
    eData[b].weight = -eData[b].weight;
  if (_has_sweep_dest_data)
    {
      int swap = swdData[b].leW;
      swdData[b].leW = swdData[b].riW;
      swdData[b].riW = swap;
    }
  if (_has_back_data)
    {
      double swat = ebData[b].tSt;
      ebData[b].tSt = ebData[b].tEn;
      ebData[b].tEn = swat;
    }
  if (_has_voronoi_data)
    {
      int swai = voreData[b].leF;
      voreData[b].leF = voreData[b].riF;
      voreData[b].riF = swai;
    }
}
void
Shape::CalcBBox (bool strict_degree)
{
  if (hasPoints() == false)
  {
    leftX = rightX = topY = bottomY = 0;
    return;
  }
  leftX = rightX = getPoint(0).x[0];
  topY = bottomY = getPoint(0).x[1];
  bool not_set=true;
  for (int i = 0; i < numberOfPoints(); i++)
  {
    if ( strict_degree == false || getPoint(i).dI > 0 || getPoint(i).dO > 0 ) {
      if ( not_set ) {
        leftX = rightX = getPoint(i).x[0];
        topY = bottomY = getPoint(i).x[1];
        not_set=false;
      } else {
        if (  getPoint(i).x[0] < leftX) leftX = getPoint(i).x[0];
        if (  getPoint(i).x[0] > rightX) rightX = getPoint(i).x[0];
        if (  getPoint(i).x[1] < topY) topY = getPoint(i).x[1];
        if (  getPoint(i).x[1] > bottomY) bottomY = getPoint(i).x[1];
      }
    }
  }
}

/** Returns true iff the L2 distance from \a thePt to this shape is <= \a max_l2.
*  Distance = the min of distance to its points and distance to its edges.
*  Points without edges are considered, which is maybe unwanted...
*/
bool Shape::DistanceLE(NR::Point const thePt, double const max_l2)
{
  if ( hasPoints() == false ) {
    return false;
  }
  
  /* TODO: Consider using bbox to return early, perhaps conditional on nbPt or nbAr. */
  
  /* Test thePt against pts[i].x for all i. */
  {
    /* effic: In one test case (scribbling with the freehand tool to create a small number of long
    * path elements), changing from a Distance method to a DistanceLE method reduced this
* function's CPU time from about 21% of total inkscape CPU time to 14-15% of total inkscape
* CPU time, due to allowing early termination.  I don't know how much the L1 test helps, it
* may well be a case of premature optimization.  Consider testing dot(offset, offset)
* instead.
*/
    double const max_l1 = max_l2 * M_SQRT2;
    for (int i = 0; i < numberOfPoints(); i++) {
      NR::Point const offset( thePt - getPoint(i).x );
      double const l1 = NR::L1(offset);
      if ( ( l1 <= max_l2 )
           || ( ( l1 <= max_l1 )
                && ( NR::L2(offset) <= max_l2 ) ) )
      {
        return true;
      }
    }
  }
  
  for (int i = 0; i < numberOfEdges(); i++) {
    if ( getEdge(i).st >= 0 &&
         getEdge(i).en >= 0 ) {
      NR::Point const st(getPoint(getEdge(i).st).x);
      NR::Point const en(getPoint(getEdge(i).en).x);
      NR::Point const d( thePt - st );
      NR::Point const e( en - st );
      double const el = NR::L2(e);
      if ( el > 0.001 ) {
        NR::Point const e_unit( e / el );
        double const npr = NR::dot(d, e_unit);
        if ( npr > 0 && npr < el ) {
          double const nl = fabs( NR::cross(d, e_unit) );
          if ( nl <= max_l2 ) {
            return true;
          }
        }
      }
    }
  }
  return false;
}
/** Returns true iff the L2 distance from \a thePt to this shape is <= \a max_l2.
*  Distance = the min of distance to its points and distance to its edges.
*  Points without edges are considered, which is maybe unwanted...
*/
double Shape::Distance(NR::Point const thePt)
{
  if ( hasPoints() == false) {
    return 0.0;
  }
  
  double bdot=NR::dot(thePt-getPoint(0).x,thePt-getPoint(0).x);
  {
    for (int i = 0; i < numberOfPoints(); i++) {
      NR::Point const offset( thePt - getPoint(i).x );
      double ndot=NR::dot(offset,offset);
      if ( ndot < bdot ) {
        bdot=ndot;
      }
    }
  }
  
  for (int i = 0; i < numberOfEdges(); i++) {
    if ( getEdge(i).st >= 0 &&
         getEdge(i).en >= 0 ) {
      NR::Point const st(getPoint(getEdge(i).st).x);
      NR::Point const en(getPoint(getEdge(i).en).x);
      NR::Point const d( thePt - st );
      NR::Point const e( en - st );
      double const el = NR::dot(e,e);
      if ( el > 0.001 ) {
        double const npr = NR::dot(d, e);
        if ( npr > 0 && npr < el ) {
          double const nl = fabs( NR::cross(d, e) );
          double ndot=nl*nl/el;
          if ( ndot < bdot ) {
            bdot=ndot;
          }
        }
      }
    }
  }
  return sqrt(bdot);
}

// winding of a point with respect to the Shape
// 0= outside
// 1= inside (or -1, that usually the same)
// other=depends on your fill rule
// if the polygon is uncrossed, it's all the same, usually
int
Shape::PtWinding (const NR::Point px) const 
{
  int lr = 0, ll = 0, rr = 0;
  
  for (int i = 0; i < numberOfEdges(); i++)
  {
    NR::Point const adir = getEdge(i).dx;

    NR::Point const ast = getPoint(getEdge(i).st).x;
    NR::Point const aen = getPoint(getEdge(i).en).x;
    
    //int const nWeight = eData[i].weight;
    int const nWeight = 1;

    if (ast[0] < aen[0]) {
      if (ast[0] > px[0]) continue;
      if (aen[0] < px[0]) continue;
    } else {
      if (ast[0] < px[0]) continue;
      if (aen[0] > px[0]) continue;
    }
    if (ast[0] == px[0]) {
      if (ast[1] >= px[1]) continue;
      if (aen[0] == px[0]) continue;
      if (aen[0] < px[0]) ll += nWeight;  else rr -= nWeight;
      continue;
    }
    if (aen[0] == px[0]) {
      if (aen[1] >= px[1]) continue;
      if (ast[0] == px[0]) continue;
      if (ast[0] < px[0]) ll -= nWeight; else rr += nWeight;
      continue;
    }
    
    if (ast[1] < aen[1]) {
      if (ast[1] >= px[1])  continue;
    } else {
      if (aen[1] >= px[1]) continue;
    }

    NR::Point const diff = px - ast;
    double const cote = cross(diff, adir);
    if (cote == 0) continue;
    if (cote < 0) {
      if (ast[0] > px[0]) lr += nWeight;
    } else {
      if (ast[0] < px[0]) lr -= nWeight;
    }
  }
  return lr + (ll + rr) / 2;
}

//};
