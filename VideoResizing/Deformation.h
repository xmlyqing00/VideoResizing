#ifndef DEFORMATION_H
#define DEFORMATION_H

#include <map>
#include <ctime>
#include "common.h"
#include "KeyFrame.h"
#include "ControlPoint.h"

typedef pair<int, int> Edge;

class Deformation {

private:
	int frameNum;
	Size frameSize;
	vector<KeyFrame> &frames;
	vector<Point2f> staticPoints;

	int controlPointsNum;
	vector<ControlPoint> controlPoints;
	vector< vector<int> > frameControlPointIndex;

	double deformedScaleX, deformedScaleY;
	Size deformedFrameSize;

	vector<Mat> deformedFrames;

	void DrawSubdiv( const Mat &, const Subdiv2D & );
	void DrawEdge( int, int );
	void DrawLocate( const Point2f &, const vector<BaryCoord> & );

	void CalcBaryCooordLambda( const Point2f &, const vector<Point2f> &, vector<double> & );
	int LocateNearestPoint( int, const Point2f &, vector<BaryCoord> & );
	Point2f CalcPointByBaryCoord( const vector<BaryCoord> & );

	Point2f GetBoundPoint( int, int );
	void DelaunayDivide();
	void AddTemporalNeighbors();

public:

	enum {
		ORIGIN_POS = 0,
		DEFORMED_POS = 1,
		ORIGIN_POS_WITH_FRAME = 2,
		DEFORMED_POS_WITH_FRAME = 3
	} POS_TYPE;

	vector<Mat> deformedMap;

	Deformation( vector<KeyFrame> & );
	void BuildControlPoints();
	
	void InitDeformation( double, double );
	double CalcEnergy();
	void MinimizeEnergy();

	void CalcDeformedMap();
	void RenderFrame( const Mat &, const Mat &, Mat & );
	void RenderKeyFrames();

};

#endif