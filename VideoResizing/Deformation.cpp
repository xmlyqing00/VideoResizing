#include "Deformation.h"

Deformation::Deformation( vector<KeyFrame> &_frames, const string &_videoName ) :frames( _frames ) {

	videoName = _videoName;
	frameNum = _frames.size();
	frameSize = _frames[0].img.size();

	staticPoints.push_back( Point2f( 0, 0 ) );
	staticPoints.push_back( Point2f( frameSize.width - 1, 0 ) );
	staticPoints.push_back( Point2f( 0, frameSize.height - 1 ) );
	staticPoints.push_back( Point2f( frameSize.width - 1, frameSize.height - 1 ) );

	controlPointsNum = 0;
	freeControlPointsNum = 0;
	controlPoints.clear();
	centerControlPointIndex.clear();
	temporalControlPointIndex.clear();
	spatialEdges.clear();
	frameControlPointIndex = vector< vector<int> >( frameNum );

	for ( auto &frame : frames ) {
		frame.FreeMemory();
	}

}

void Deformation::DrawSubdiv( const Mat &_img, const Subdiv2D &subdiv ) {

	Scalar delaunay_color( 255, 255, 255 );
	Mat img = _img.clone();

	vector<Vec4f> edgeList;
	subdiv.getEdgeList( edgeList );
	for ( size_t i = 0; i < edgeList.size(); i++ ) {
		Vec4f e = edgeList[i];
		Point pt0 = Point( cvRound( e[0] ), cvRound( e[1] ) );
		Point pt1 = Point( cvRound( e[2] ), cvRound( e[3] ) );
		line( img, pt0, pt1, delaunay_color, 1, CV_AA, 0 );
	}

	imshow( "SubDiv", img );
	waitKey( 1 );

}

void Deformation::DrawEdge( int frameId, int posType, Mat &_img ) {

	Mat img;
	Scalar lineColor( 255, 255, 255 );
	Scalar centerColor( 0, 0, 255 );
	Scalar boundColor( 255, 0, 0 );
	Scalar staticColor( 0, 255, 0 );

	switch ( posType ) {
		case ORIGIN_POS:
			img = Mat::zeros( frameSize, CV_8UC3 );
			break;
		case DEFORMED_POS:
			img = Mat::zeros( deformedFrameSize, CV_8UC3 );
			break;
		case ORIGIN_POS_WITH_FRAME:
			img = frames[frameId].img.clone();
			break;
		case DEFORMED_POS_WITH_FRAME:
			img = deformedFrames[frameId].clone();
			break;
		default:
			break;
	}

	for ( const auto &controlPointIndex : frameControlPointIndex[frameId] ) {

		const ControlPoint &controlPoint = controlPoints[controlPointIndex];

		if ( controlPoint.anchorType != ControlPoint::ANCHOR_CENTER ) continue;

		if ( posType == ORIGIN_POS || posType == ORIGIN_POS_WITH_FRAME ) {
			circle( img, controlPoint.originPos, 3, centerColor, 2, CV_AA );
		} else {
			circle( img, controlPoint.pos, 3, centerColor, 2, CV_AA );
		}

		for ( const auto &neighborIndex : controlPoint.boundNeighbors ) {

			const ControlPoint &neighborPoint = controlPoints[neighborIndex];

			if ( neighborPoint.anchorType == ControlPoint::ANCHOR_CENTER ) {
				cout << controlPoint.originPos << " " << neighborPoint.originPos << endl;
			}

			if ( posType == ORIGIN_POS || posType == ORIGIN_POS_WITH_FRAME ) {

				line( img, controlPoint.originPos, neighborPoint.originPos, lineColor, 1, CV_AA );
				if ( neighborPoint.anchorType == ControlPoint::ANCHOR_BOUND ) {
					circle( img, neighborPoint.originPos, 3, boundColor, 2, CV_AA );
				} else {
					circle( img, neighborPoint.originPos, 3, staticColor, 2, CV_AA );
				}
				
			} else {

				line( img, controlPoint.pos, neighborPoint.pos, lineColor, 1, CV_AA );
				if ( neighborPoint.anchorType == ControlPoint::ANCHOR_BOUND ) {
					circle( img, neighborPoint.pos, 3, boundColor, 2, CV_AA );
				} else {
					circle( img, neighborPoint.pos, 3, staticColor, 2, CV_AA );
				}
				
			}

		}

	}

	_img = img.clone();

}

void Deformation::DrawLocate( const Point2f &deformedPoint, const vector<BaryCoord> &baryCoord ) {

	Mat img = Mat::zeros( frameSize, CV_8UC3 );

	circle( img, deformedPoint, 5, Scalar( 255, 0, 0 ), 2, CV_AA );

	for ( const auto &vertex : baryCoord ) {
		ControlPoint controlPoint = controlPoints[vertex.second];
		circle( img, controlPoint.originPos, 5, Scalar( 0, 0, 128 ), 2, CV_AA );
		circle( img, controlPoint.pos, 5, Scalar( 128, 0, 0 ), 2, CV_AA );
	}

	Point2f originPoint = CalcPointByBaryCoord( baryCoord, ORIGIN_POS );
	circle( img, originPoint, 5, Scalar( 0, 0, 255 ), 2, CV_AA );

	imshow( "Locate", img );
	waitKey( 1 );

}

Point2f Deformation::GetBoundPoint( int index0, int index1 ) {

	ControlPoint p0 = controlPoints[index0];
	ControlPoint p1 = controlPoints[index1];

	if ( p0.saliency < p1.saliency ) {
		swap( p0, p1 );
	}

	int superpixelIndex = p0.superpixelIndex;
	int frameId = p0.frameId;
	Point2f neighborPoint( -1, -1 );

	if ( SignNumber( p0.originPos.x - p1.originPos.x ) == 0 ) {

		int x = p0.originPos.x;
		if ( p0.originPos.y < p1.originPos.y ) {
			for ( int y = p0.originPos.y; y < p1.originPos.y; y++ ) {
				if ( frames[frameId].pixelLabel.at<int>( y, x ) != superpixelIndex ) {
					neighborPoint = Point( x, y );
					break;
				}
			}
		} else {
			for ( int y = p0.originPos.y; y > p1.originPos.y; y-- ) {
				if ( frames[frameId].pixelLabel.at<int>( y, x ) != superpixelIndex ) {
					neighborPoint = Point( x, y );
					break;
				}
			}
		}

	} else {

		int dx = 1;
		double k = (p1.originPos.y - p0.originPos.y) / abs( p0.originPos.x - p1.originPos.x );
		if ( p0.originPos.x > p1.originPos.x ) dx = -1;

		if ( dx > 0 ) {

			for ( int x = p0.originPos.x; x < p1.originPos.x; x += dx ) {

				int y = RoundToInt( p0.originPos.y + k * abs( x - p0.originPos.x ) );
				int tmpY = RoundToInt( p0.originPos.y + k * abs( x + dx - p0.originPos.x ) );

				if ( k > 0 ) {
					while ( y <= tmpY ) {
						if ( frames[frameId].pixelLabel.at<int>( y, x ) != superpixelIndex ) {
							neighborPoint = Point( x, y );
							break;
						}
						y++;
					}
					if ( neighborPoint.x != -1 ) break;

				} else {
					
					while ( y >= tmpY ) {
						if ( frames[frameId].pixelLabel.at<int>( y, x ) != superpixelIndex ) {
							neighborPoint = Point( x, y );
							break;
						}
						y--;
					}
					if ( neighborPoint.x != -1 ) break;

				}

			}

		} else {

			for ( int x = p0.originPos.x; x > p1.originPos.x; x += dx ) {

				int y = RoundToInt( p0.originPos.y + k * abs( x - p0.originPos.x ) );
				int tmpY = RoundToInt( p0.originPos.y + k * abs( x + dx - p0.originPos.x ) );
				
				if ( k > 0 ) {
					while ( y <= tmpY ) {
						if ( frames[frameId].pixelLabel.at<int>( y, x ) != superpixelIndex ) {
							neighborPoint = Point( x, y );
							break;
						}
						y++;
					}
					if ( neighborPoint.x != -1 ) break;

				} else {
					while ( y >= tmpY ) {
						if ( frames[frameId].pixelLabel.at<int>( y, x ) != superpixelIndex ) {
							neighborPoint = Point( x, y );
							break;
						}
						y--;
					}
					if ( neighborPoint.x != -1 ) break;

				}

			}

		}

	}

	if ( neighborPoint == Point2f( -1, -1 ) ) {
		neighborPoint = 0.5 * (p0.originPos + p1.originPos);
	}

	return neighborPoint;

}

void Deformation::BuildControlPoints() {

	printf( "\tBuild key frames control points. " );

// #define DEBUG_DELAUNAY_DIVIDE

	for ( int i = 0; i < frameNum; i++ ) {

		Rect rect( 0, 0, frameSize.width, frameSize.height );
		Subdiv2D subdiv( rect );
		double superpixelMaxDist = 1.8 * sqrt( frameSize.width * frameSize.height / (double)frames[i].superpixelNum );
		controlPointsMap.push_back( Mat( frameSize, CV_32SC1, Scalar( -1 ) ) );

		// Add superpixel center points.
		frameControlPointIndex[i] = vector<int>( frames[i].superpixelNum );
		for ( int j = 0; j < frames[i].superpixelNum; j++ ) {
			double saliency = frames[i].superpixelSaliency[j];
			controlPoints.push_back( ControlPoint( i, frames[i].superpixelCenter[j], ControlPoint::ANCHOR_CENTER, j, saliency ) );
			controlPointsMap[i].at<int>( frames[i].superpixelCenter[j] ) = controlPointsNum;
			frameControlPointIndex[i][j] = controlPointsNum;
			centerControlPointIndex.push_back( controlPointsNum );

			subdiv.insert( frames[i].superpixelCenter[j] );
			controlPointsNum++;

#ifdef DEBUG_DELAUNAY_DIVIDE
			DrawSubdiv( frames[i].img, subdiv );
#endif

		}

		// Add superpixel bound points.
		vector<Vec4f> edgeList;
		subdiv.getEdgeList( edgeList );
		map< string, int> edgeExist;

		for ( const auto &e : edgeList ) {

			Point2f p0( e.val[0], e.val[1] );
			Point2f p1( e.val[2], e.val[3] );

			if ( CheckOutside( p0, frameSize ) || CheckOutside( p1, frameSize ) ) {
				continue;
			}

			int index0 = controlPointsMap[i].at<int>( p0 );
			int index1 = controlPointsMap[i].at<int>( p1 );

			string edgeHash0 = to_string( index0 ) + " " + to_string( index1 );
			string edgeHash1 = to_string( index1 ) + " " + to_string( index0 );
			if ( edgeExist.count( edgeHash0 ) > 0 ) continue;
			if ( edgeExist.count( edgeHash1 ) > 0 ) continue;
			edgeExist[edgeHash0] = 1;
			edgeExist[edgeHash1] = 1;

			if ( frames[i].superpixelBoundLabel[controlPoints[index0].superpixelIndex] != KeyFrame::BOUND_NONE &&
				 frames[i].superpixelBoundLabel[controlPoints[index1].superpixelIndex] != KeyFrame::BOUND_NONE ) {
				double superpixelDist = NormL2( p0 - p1 );
				if ( superpixelDist > superpixelMaxDist ) continue;
			}

			Point2f neighborPoint = GetBoundPoint( index0, index1 );
			controlPoints.push_back( ControlPoint( i, neighborPoint, ControlPoint::ANCHOR_BOUND, -1, -1 ) );
			controlPointsMap[i].at<int>( neighborPoint ) = controlPointsNum;

			controlPoints[index0].AddBoundNeighbor( controlPointsNum );
			controlPoints[index1].AddBoundNeighbor( controlPointsNum );
			controlPoints[controlPointsNum].AddBoundNeighbor( index0 );
			controlPoints[controlPointsNum].AddBoundNeighbor( index1 );

			controlPoints[index0].AddSuperpixelNeighbor( index1 );
			controlPoints[index1].AddSuperpixelNeighbor( index0 );

			controlPointsNum++;

		}

		freeControlPointsNum = controlPointsNum;

		// Add anchor points.
		for ( const auto &point : staticPoints ) {

			int label = frames[i].pixelLabel.at<int>( point );
			int controlPointIndex = frameControlPointIndex[i][label];

			controlPoints.push_back( ControlPoint( i, point, ControlPoint::ANCHOR_STATIC, label, -1 ) );
			controlPointsMap[i].at<int>( point ) = controlPointsNum;
			controlPoints[controlPointIndex].AddBoundNeighbor( controlPointsNum );
			controlPoints[controlPointsNum].AddBoundNeighbor( controlPointIndex );

			controlPointsNum++;

		}

		// Add image bound points.
		for ( int j = 0; j < frames[i].superpixelNum; j++ ) {

			Point2f center = frames[i].superpixelCenter[j];
			Point pointBound;
			int controlPointIndex = frameControlPointIndex[i][j];

			switch ( frames[i].superpixelBoundLabel[j] ) {

				case KeyFrame::BOUND_LEFT:
					pointBound = Point( 0, center.y );
					controlPoints.push_back( ControlPoint( i, pointBound, ControlPoint::ANCHOR_STATIC_LEFT, j, -1 ) );
					controlPointsMap[i].at<int>( pointBound ) = controlPointsNum;
					controlPoints[controlPointIndex].AddBoundNeighbor( controlPointsNum );
					controlPoints[controlPointsNum].AddBoundNeighbor( controlPointIndex );
					controlPointsNum++;
					break;

				case KeyFrame::BOUND_TOP:
					pointBound = Point( center.x, 0 );
					controlPoints.push_back( ControlPoint( i, pointBound, ControlPoint::ANCHOR_STATIC_TOP, j, -1 ) );
					controlPointsMap[i].at<int>( pointBound ) = controlPointsNum;
					controlPoints[controlPointIndex].AddBoundNeighbor( controlPointsNum );
					controlPoints[controlPointsNum].AddBoundNeighbor( controlPointIndex );
					controlPointsNum++;
					break;

				case KeyFrame::BOUND_RIGHT:
					pointBound = Point( frameSize.width - 1, center.y );
					controlPoints.push_back( ControlPoint( i, pointBound, ControlPoint::ANCHOR_STATIC_RIGHT, j, -1 ) );
					controlPointsMap[i].at<int>( pointBound ) = controlPointsNum;
					controlPoints[controlPointIndex].AddBoundNeighbor( controlPointsNum );
					controlPoints[controlPointsNum].AddBoundNeighbor( controlPointIndex );
					controlPointsNum++;
					break;

				case KeyFrame::BOUND_BOTTOM:
					pointBound = Point( center.x, frameSize.height - 1 );
					controlPoints.push_back( ControlPoint( i, pointBound, ControlPoint::ANCHOR_STATIC_BOTTOM, j, -1 ) );
					controlPointsMap[i].at<int>( pointBound ) = controlPointsNum;
					controlPoints[controlPointIndex].AddBoundNeighbor( controlPointsNum );
					controlPoints[controlPointsNum].AddBoundNeighbor( controlPointIndex );
					controlPointsNum++;
					break;

				case KeyFrame::BOUND_NONE:
					break;
				default:
					break;
			}
		}

#ifdef DEBUG_DELAUNAY_DIVIDE
		Mat img;
		DrawEdge( i, ORIGIN_POS_WITH_FRAME, img );
		imshow( "Edge", img );
		waitKey( 0 );
#endif

	}

	printf( "Control point num: %d.\n", controlPoints.size() );

}

void Deformation::AddSpatialNeighbors() {

// #define DEBUG_ADD_SPATIAL_NEIGHBORS

	printf( "\tAdd spatial neighbors to control points.\n" );

	int frameId = -1;
	Mat cannyImg, visitedMap;
	
	for ( int controlPointIndex = 0; controlPointIndex < controlPointsNum; controlPointIndex++ ) {

		ControlPoint &controlPoint = controlPoints[controlPointIndex];

		if ( frameId != controlPoint.frameId ) {
			
			frameId = controlPoint.frameId;
			Canny( frames[frameId].grayImg, cannyImg, 70, 140 );
			visitedMap = Mat( frameSize, CV_32SC1, Scalar(-1) );
#ifdef DEBUG_ADD_SPATIAL_NEIGHBORS
			//imshow( "canny", cannyImg );
			//waitKey( 0 );
#endif

		}

		queue<Point> que;
		Point seedPoint( controlPoint.originPos );
		vector<int> spatialEdge;

		if ( cannyImg.at<uchar>( controlPoint.originPos ) == 1 &&
			 visitedMap.at<int>( seedPoint ) == -1 ) {
			visitedMap.at<int>( seedPoint ) = controlPointIndex;
			que.push( seedPoint );
		}

		for ( int k = 0; k < NEIGHBORS_NUM; k++ ) {
			
			Point nextPos = seedPoint + neighbors[k];
			if ( CheckOutside( nextPos, frameSize ) ) continue;
			if ( cannyImg.at<uchar>( nextPos ) == 0 ) continue;
			if ( visitedMap.at<int>( nextPos ) != -1 )continue;

			visitedMap.at<int>( nextPos ) = controlPointIndex;
			que.push( nextPos );

		}
		
		if ( !que.empty() ) spatialEdge.push_back( controlPointIndex );

		while ( !que.empty() ) {

			Point curPos = que.front();
			que.pop();

			for ( int k = 0; k < NEIGHBORS_NUM; k++ ) {

				Point nextPos = curPos + neighbors[k];
				if ( CheckOutside( nextPos, frameSize ) ) continue;

				int nextControlPointIndex = controlPointsMap[frameId].at<int>( nextPos );
				if ( nextControlPointIndex != -1 ) spatialEdge.push_back( nextControlPointIndex );
			}

			for ( int k = 0; k < NEIGHBORS_NUM; k++ ) {

				Point nextPos = curPos + neighbors[k];
				if ( CheckOutside( nextPos, frameSize ) ) continue;
				if ( cannyImg.at<uchar>( nextPos ) == 0 ) continue;
				if ( visitedMap.at<int>( nextPos ) != -1 )continue;

				visitedMap.at<int>( nextPos ) = controlPointIndex;
				que.push( nextPos );

			}

		}

		// Erase duplications.
		sort( spatialEdge.begin(), spatialEdge.end() );

#ifdef DEBUG_ADD_SPATIAL_NEIGHBORS
		if ( spatialEdge.size() > 1 ) {
			for ( size_t i = 0; i < spatialEdge.size(); i++ ) {
				cout << spatialEdge[i] << " ";
			}
			cout << endl;
		}
#endif

		for ( size_t i = 1; i < spatialEdge.size(); i++ ) {
			if ( spatialEdge[i] == spatialEdge[i - 1] ) {
				spatialEdge.erase( spatialEdge.begin() + i );
				i--;
			}
		}

		if ( spatialEdge.size() > 1 ) {
			spatialEdges.push_back( spatialEdge );
		}

	}	

#ifdef DEBUG_ADD_SPATIAL_NEIGHBORS

	cout << endl << endl;

	for ( const auto &spatialEdge : spatialEdges ) {
		for ( const auto &controlPointIndex : spatialEdge ) {
			cout << controlPointIndex << " ";
		}
		cout << endl;
	}

	for ( const auto &spatialEdge: spatialEdges ) {
		Mat debugImg = frames[controlPoints[spatialEdge[0]].frameId].img.clone();
		for ( const auto &controlPointIndex : spatialEdge ) {
			ControlPoint &p = controlPoints[controlPointIndex];
			circle( debugImg, p.originPos, 2, Scalar( 0, 0, 255 ), 2 );
		}
		imshow( "Spatial Neighbors", debugImg );
		waitKey( 0 );
	}
#endif

}

void Deformation::CalcBaryCoordLambda( const Point2f &p, vector<Point2f> &vertices, vector<double> &lambda ) {

	if ( vertices.size() == 3 ) {

		double detT = (vertices[1].y - vertices[2].y) * (vertices[0].x - vertices[2].x) +
			(vertices[2].x - vertices[1].x) * (vertices[0].y - vertices[2].y);

		if ( SignNumber( detT ) == 0 ) {
			vertices.erase( vertices.begin() );
			lambda.erase( lambda.begin() );
			CalcBaryCoordLambda( p, vertices, lambda );
			return;
		}

		lambda[0] = ((vertices[1].y - vertices[2].y) * (p.x - vertices[2].x) +
					  (vertices[2].x - vertices[1].x) * (p.y - vertices[2].y)) / detT;
		lambda[1] = ((vertices[2].y - vertices[0].y) * (p.x - vertices[2].x) +
					  (vertices[0].x - vertices[2].x) * (p.y - vertices[2].y)) / detT;
		lambda[2] = 1 - lambda[0] - lambda[1];

	} else if ( vertices.size() == 2 ) {

		double detT = vertices[0].x * vertices[1].y - vertices[0].y * vertices[1].x;

		if ( SignNumber( detT ) == 0 ) {
			vertices.erase( vertices.begin() );
			lambda.erase( lambda.begin() );
			lambda[0] = 1;
			return;
		}

		if ( SignNumber( detT ) == 0 ) {
			if ( SignNumber( vertices[0].x + vertices[1].x ) != 0 ) {
				lambda[0] = (p.x - vertices[1].x) / (vertices[0].x + vertices[1].x);
				lambda[1] = 1 - lambda[0];
			} else if ( SignNumber( vertices[0].y + vertices[1].y ) != 0 ) {
				lambda[0] = (p.y - vertices[1].y) / (vertices[0].y + vertices[1].y);
				lambda[1] = 1 - lambda[0];
			}
		} else {
			lambda[0] = (p.x * vertices[1].y - p.y * vertices[1].x) / detT;
			lambda[1] = (p.y * vertices[0].x - p.x * vertices[0].y) / detT;
		}
	}

}

void Deformation::CalcBaryCoord1( const Mat &cpMap, const Point2f &p, vector<BaryCoord> &baryCoord ) {
	int controlPointIndex = cpMap.at<int>( p );
	if ( controlPointIndex == -1 ) {
		cout << "1 " << p << endl;
	}
	baryCoord.push_back( make_pair( 1, controlPointIndex ) );
}

void Deformation::CalcBaryCoord2( Subdiv2D &subdiv, const Mat &cpMap, int e0, const Point2f &p, vector<BaryCoord> &baryCoord ) {

	Point2f pointOrg, pointDst;
	vector<Point2f> vertices;

	if ( subdiv.edgeOrg( e0, &pointOrg ) > 0 && subdiv.edgeDst( e0, &pointDst ) > 0 ) {
		RestrictInside( pointOrg, frameSize );
		RestrictInside( pointDst, frameSize );
		vertices.push_back( pointOrg );
		vertices.push_back( pointDst );
	} else {
		cout << "[CalcBaryCoord2] Get points error: pointOrg " << subdiv.edgeOrg( e0, &pointOrg ) << ", pointDst " << subdiv.edgeDst( e0, &pointDst ) << endl;
	}

	vector<double> lambda( vertices.size() );

	CalcBaryCoordLambda( p, vertices, lambda );

	for ( size_t i = 0; i < vertices.size(); i++ ) {
		int vertex = cpMap.at<int>( vertices[i] );
		if ( vertex == -1 ) {
			cout << "2 " << vertices[i] << endl;
		}
		baryCoord.push_back( make_pair( lambda[i], vertex ) );
	}

#ifdef DEBUG
	//printf( "Bi vertices X: %.3lf = %.3lf * %.3lf + %.3lf * %.3lf\n", nextFramePos.x, lambda[0], biVertices[0].x, lambda[1], biVertices[1].x );
	//printf( "Bi vertices Y: %.3lf = %.3lf * %.3lf + %.3lf * %.3lf\n", nextFramePos.y, lambda[0], biVertices[0].y, lambda[1], biVertices[1].y );
	//double tmpX = 0, tmpY = 0;
	//for ( int i = 0; i < 3; i++ ) {
	//	tmpX += lambda[i] * biVertices[i].x;
	//	tmpY += lambda[i] * biVertices[i].y;
	//}
	//cout << Point2f( tmpX, tmpY ) << endl;
#endif

}

void Deformation::CalcBaryCoord3( Subdiv2D &subdiv, const Mat &cpMap, int e0, const Point2f &p, vector<BaryCoord> &baryCoord ) {

	vector<Point2f> vertices;
	int e = e0;

	do {
		Point2f pointOrg, pointDst;
		if ( subdiv.edgeOrg( e, &pointOrg ) > 0 && subdiv.edgeDst( e, &pointDst ) > 0 ) {

			bool vertexExistFlag = false;
			RestrictInside( pointOrg, frameSize );
			for ( const auto &vertex : vertices ) {
				if ( vertex == pointOrg ) {
					vertexExistFlag = true;
					break;
				}
			}
			if ( !vertexExistFlag ) {
				vertices.push_back( pointOrg );
				if ( vertices.size() >= 3 ) break;
			}

			vertexExistFlag = false;
			RestrictInside( pointDst, frameSize );
			for ( const auto &vertex : vertices ) {
				if ( vertex == pointDst ) {
					vertexExistFlag = true;
					break;
				}
			}
			if ( !vertexExistFlag ) {
				vertices.push_back( pointDst );
				if ( vertices.size() >= 3 ) break;
			}

		}

		e = subdiv.getEdge( e, Subdiv2D::NEXT_AROUND_LEFT );

	} while ( e != e0 );

	vector<double> lambda( vertices.size() );

	CalcBaryCoordLambda( p, vertices, lambda );

#ifdef DEBUG
	/*cout << nextFramePos << endl;
	printf( "%.3lf %.3lf %.3lf\n", lambda[0], lambda[1], lambda[2] );
	cout << triVertices[0] << " " << triVertices[1] << " " << triVertices[2] << endl;
	double tmpX = 0, tmpY = 0;
	for ( int i = 0; i < 3; i++ ) {
	tmpX += lambda[i] * triVertices[i].x;
	tmpY += lambda[i] * triVertices[i].y;
	}
	cout << Point2f( tmpX, tmpY ) << endl;*/
#endif

	for ( size_t i = 0; i < vertices.size(); i++ ) {
		int vertex = cpMap.at<int>( vertices[i] );
		if ( vertex == -1 ) {
			cout << "3 " << vertices[i] << endl;
		}
		baryCoord.push_back( make_pair( lambda[i], vertex ) );
	}

}

int Deformation::LocatePoint( Subdiv2D &subdiv, const Mat &cpMap, const Point2f &p, vector<BaryCoord> &baryCoord ) {

//#define DEBUG_LOCATE_POINT

	int e0, vertex, locateStatus;

	baryCoord.clear();
	locateStatus = subdiv.locate( p, e0, vertex );

	switch ( locateStatus ) {
		case CV_PTLOC_INSIDE:
			CalcBaryCoord3( subdiv, cpMap, e0, p, baryCoord );
			break;
		case CV_PTLOC_ON_EDGE:
			CalcBaryCoord2( subdiv, cpMap, e0, p, baryCoord );
			break;
		case CV_PTLOC_VERTEX:
			CalcBaryCoord1( cpMap, p, baryCoord );
			break;
		default:
			break;
	}

#ifdef DEBUG_LOCATE_POINT
	Point2f tmpP0 = CalcPointByBaryCoord( baryCoord, ORIGIN_POS );
	Point2f tmpP1 = CalcPointByBaryCoord( baryCoord, DEFORMED_POS );
	cout << p << " " << tmpP0 << " " << tmpP1 << endl;
	for ( const auto &coord : baryCoord ) {
		cout << coord.first << " " << controlPoints[coord.second].originPos << " " << controlPoints[coord.second].pos << endl;
	}
	cout << endl;
	// DrawLocate( p, baryCoord );
#endif

	return locateStatus;

}

void Deformation::AddTemporalNeighbors() {

	printf( "\tAdd temporal neighbors to control points.\n" );

	int curFramePointIndex = 0;
	int nextFramePointIndex = 0;
	Rect rect( 0, 0, frameSize.width, frameSize.height );
	
	for ( ; nextFramePointIndex < controlPointsNum; nextFramePointIndex++ ) {
		if ( controlPoints[nextFramePointIndex].frameId != 0 ) break;
	}

	for ( int frameId = 0; frameId < frameNum - 1; frameId++ ) {

		Subdiv2D subdiv( rect );

		for ( ; nextFramePointIndex < controlPointsNum; nextFramePointIndex++ ) {
			
			ControlPoint &controlPoint = controlPoints[nextFramePointIndex];
			if ( controlPoint.frameId == frameId + 2 ) break;
			subdiv.insert( controlPoint.originPos );

		}

		for ( ; curFramePointIndex < controlPointsNum; curFramePointIndex++ ) {
			
			ControlPoint &controlPoint = controlPoints[curFramePointIndex];
			if ( controlPoint.anchorType != ControlPoint::ANCHOR_CENTER && controlPoint.anchorType != ControlPoint::ANCHOR_BOUND ) continue;
			if ( controlPoint.frameId != frameId ) break;

			Point2f flow = frames[controlPoint.frameId].forwardFlowMap.at<Point2f>( Point( controlPoint.originPos ) );
			Point2f nextFramePos = controlPoint.originPos + flow;
			controlPoint.flow = Point2f( flow.x * deformedScaleX, flow.y * deformedScaleY );

			if ( CheckOutside( nextFramePos, frameSize ) ) continue;

			vector<BaryCoord> baryCoord;
			int locateStatus = LocatePoint( subdiv, controlPointsMap[frameId + 1], nextFramePos, baryCoord );
			if ( locateStatus == CV_PTLOC_INSIDE || locateStatus == CV_PTLOC_ON_EDGE || locateStatus == CV_PTLOC_VERTEX ) {
				controlPoint.AddTemporalNeighbor( baryCoord );
				temporalControlPointIndex.push_back( curFramePointIndex );
			}
			
		}

	}
	
}

void Deformation::InitDeformation( double _deformedScaleX, double _deformedScaleY ) {

	printf( "Initialize deformation.\n" );

	deformedScaleX = _deformedScaleX;
	deformedScaleY = _deformedScaleY;
	deformedFrameSize = Size( CeilToInt( frameSize.width * deformedScaleX ), CeilToInt( frameSize.height * deformedScaleY ) );

	BuildControlPoints();
	AddSpatialNeighbors();
	AddTemporalNeighbors();

	for ( auto &controlPoint : controlPoints ) {
		controlPoint.pos.x *= deformedScaleX;
		controlPoint.pos.y *= deformedScaleY;
	}

#ifdef DEBUG_INIT_DEFORMATION
	//DrawEdge( ORIGIN_POS );
	//DrawEdge( DEFORMED_POS );
#endif

}

double Deformation::CalcSaliencyEnergy() {

	double saliencyEnergy = 0;

	for ( const auto &controlPointIndex : centerControlPointIndex ) {

		double tmpEnergy = 0;
		ControlPoint &centerPoint = controlPoints[controlPointIndex];

		for ( const auto &boundPointIndex : centerPoint.boundNeighbors ) {

			ControlPoint &boundPoint = controlPoints[boundPointIndex];
			tmpEnergy += SqrNormL2( (centerPoint.pos - boundPoint.pos) - (centerPoint.originPos - boundPoint.originPos) );

		}

		saliencyEnergy += centerPoint.saliency * tmpEnergy;

	}

	return saliencyEnergy;

}

double Deformation::CalcSpatialEnergy() {

	double spatialEnergy = 0;

	for ( const auto &spatialEdge : spatialEdges ) {

		Point2f sum( 0, 0 );
		Point2f squaredSum( 0, 0 );
		for ( const auto &controlPointIndex : spatialEdge ) {
			const ControlPoint &p = controlPoints[controlPointIndex];
			sum.x += (double)p.pos.x / p.originPos.x;
			sum.y += (double)p.pos.y / p.originPos.y;
			squaredSum.x += sqr( (double)p.pos.x / p.originPos.x );
			squaredSum.y += sqr( (double)p.pos.y / p.originPos.y );
		}
		
		spatialEnergy += 1.0f / spatialEdge.size() * (squaredSum.x + squaredSum.y) 
			- 1.0f / sqr( spatialEdge.size() ) *(sqr( sum.x ) + sqr( sum.y ));
		
	}

	return spatialEnergy;

}

double Deformation::CalcTemporalEnergy() {

// #define DEBUG_CALC_TEMPORAL

	double temporalEnergy = 0;

	for ( auto &controlPointIndex : temporalControlPointIndex ) {

		ControlPoint &controlPoint = controlPoints[controlPointIndex];

		Point2f nextFramePointPos = CalcPointByBaryCoord( controlPoint.temporalNeighbors, DEFORMED_POS );
		
#ifdef DEBUG_CALC_TEMPORAL
		cout << temporalEnergy << " " << nextFramePointPos << " " << controlPoint.pos << " " << controlPoint.flow << endl;
		for ( size_t i = 0; i < controlPoint.temporalNeighbors.size(); i++ ) {
			cout << "\tneighbors " << controlPoint.temporalNeighbors[i].first << " " << controlPoints[controlPoint.temporalNeighbors[i].second].pos << endl;
		}
#endif

		temporalEnergy += SqrNormL2( (nextFramePointPos - controlPoint.pos) - controlPoint.flow );

	}

	return temporalEnergy;

}

double Deformation::CalcEnergy() {

#define DEBUG_CALC_ENERGY

	double saliencyEnergy = CalcSaliencyEnergy();
	double spatialEnergy = CalcSpatialEnergy();
	double temporalEnergy = CalcTemporalEnergy();

#ifdef DEBUG_CALC_ENERGY
	printf( "SaliencyE %.3lf, SpatialE %.3lf, TemporalE %.3lf\n", saliencyEnergy, spatialEnergy, temporalEnergy );
#endif
	double energy = ALPHA_SALIENCY * saliencyEnergy + ALPHA_SPATIAL * spatialEnergy + ALPHA_TEMPORAL * temporalEnergy;

	return energy;

}

void Deformation::AddSaliencyConstraints( Mat &coefMat, Mat &constVec ) {

	for ( auto &centerPointIndex : centerControlPointIndex ) {

		ControlPoint &centerPoint = controlPoints[centerPointIndex];

		double saliencySum = ALPHA_SALIENCY * 2 * centerPoint.saliency * centerPoint.boundNeighbors.size();

		// centerPoint row
		coefMat.at<float>( 2 * centerPointIndex, 2 * centerPointIndex ) += saliencySum;
		coefMat.at<float>( 2 * centerPointIndex + 1, 2 * centerPointIndex + 1 ) += saliencySum;

		for ( auto &boundPointIndex : centerPoint.boundNeighbors ) {

			ControlPoint &boundPoint = controlPoints[boundPointIndex];

			if ( boundPoint.anchorType == ControlPoint::ANCHOR_BOUND ) {

				// centerPoint row
				coefMat.at<float>( 2 * centerPointIndex, 2 * boundPointIndex ) -= ALPHA_SALIENCY * 2 * saliencySum;
				coefMat.at<float>( 2 * centerPointIndex + 1, 2 * boundPointIndex + 1 ) -= ALPHA_SALIENCY * 2 * saliencySum;

				// boundPoint row
				coefMat.at<float>( 2 * boundPointIndex, 2 * centerPointIndex ) -= ALPHA_SALIENCY * 2 * centerPoint.saliency;
				coefMat.at<float>( 2 * boundPointIndex, 2 * boundPointIndex ) += ALPHA_SALIENCY * 2 * centerPoint.saliency;
				coefMat.at<float>( 2 * boundPointIndex + 1, 2 * centerPointIndex + 1 ) -= ALPHA_SALIENCY * 2 * centerPoint.saliency;
				coefMat.at<float>( 2 * boundPointIndex + 1, 2 * boundPointIndex + 1 ) += ALPHA_SALIENCY * 2 * centerPoint.saliency;

				// centerPoint row
				constVec.at<float>( 2 * centerPointIndex, 1 ) += ALPHA_SALIENCY * 2 * centerPoint.saliency * (centerPoint.pos.x - boundPoint.pos.x);
				constVec.at<float>( 2 * centerPointIndex + 1, 1 ) += ALPHA_SALIENCY * 2 * centerPoint.saliency * (centerPoint.pos.y - boundPoint.pos.y);

				// boundPoint row
				constVec.at<float>( 2 * boundPointIndex, 1 ) -= ALPHA_SALIENCY * 2 * centerPoint.saliency * (centerPoint.pos.x - boundPoint.pos.x);
				constVec.at<float>( 2 * boundPointIndex + 1, 1 ) -= ALPHA_SALIENCY * 2 * centerPoint.saliency * (centerPoint.pos.x - boundPoint.pos.x);

			} else {

				// centerPoint row
				constVec.at<float>( 2 * centerPointIndex, 1 ) += ALPHA_SALIENCY * 2 * centerPoint.saliency * boundPoint.pos.x;
				constVec.at<float>( 2 * centerPointIndex + 1, 1 ) += ALPHA_SALIENCY * 2 * centerPoint.saliency * boundPoint.pos.y;

			}

		}

	}
}

void Deformation::AddSpatialConstraints( Mat &coefMat, Mat &constVec ) {

	for ( const auto &spatialEdge : spatialEdges ) {

		int spatialNum = spatialEdge.size();
		for ( const auto &controlPointIndex : spatialEdge ) {
			
			if ( controlPoints[controlPointIndex].anchorType != ControlPoint::ANCHOR_CENTER &&
				 controlPoints[controlPointIndex].anchorType != ControlPoint::ANCHOR_BOUND ) {
				continue;
			}
			for ( const auto &colIndex : spatialEdge ) {

				if ( controlPointIndex == colIndex ) {
					coefMat.at<float>( 2 * controlPointIndex, 2 * colIndex ) = ALPHA_SPATIAL * 2.0f * (spatialNum - 1) / sqr( spatialNum );
					coefMat.at<float>( 2 * controlPointIndex + 1, 2 * colIndex + 1 ) = ALPHA_SPATIAL * 2.0f * (spatialNum - 1) / sqr( spatialNum );
				} else {

					if ( controlPoints[colIndex].anchorType != ControlPoint::ANCHOR_CENTER &&
						 controlPoints[colIndex].anchorType != ControlPoint::ANCHOR_BOUND ) {
						constVec.at<float>( 2 * controlPointIndex, 1 ) += ALPHA_SPATIAL * 2.0f / sqr( spatialNum );
						constVec.at<float>( 2 * controlPointIndex + 1, 1 ) += ALPHA_SPATIAL * 2.0f / sqr( spatialNum );
					} else {
						coefMat.at<float>( 2 * controlPointIndex, 2 * colIndex ) = -ALPHA_SPATIAL * 2.0f / sqr( spatialNum );
						coefMat.at<float>( 2 * controlPointIndex + 1, 2 * colIndex + 1 ) = -ALPHA_SPATIAL * 2.0f / sqr( spatialNum );
					}
					
				}
			}
			
		}

	}

}
void Deformation::AddTemporalConstraints( Mat &coefMat, Mat &constVec ) {

}

void Deformation::OptimizeEnergyFunction( vector<Point2f> &newControlPoints ) {

	Mat coefMat( 2 * freeControlPointsNum, 2 * freeControlPointsNum, CV_32FC1, Scalar( 0 ) );
	Mat constVec( 2 * freeControlPointsNum, 1, CV_32FC1, Scalar( 0 ) );

	AddSaliencyConstraints( coefMat, constVec );
	AddSpatialConstraints( coefMat, constVec );
	AddTemporalConstraints( coefMat, constVec );

	Mat resVec;
	solve( coefMat, constVec, resVec, DECOMP_NORMAL );

}


void Deformation::CollinearConstraints( vector<Point2f> &newControlPoints ) {

	// #define DEBUG_COLLINEAR

	for ( size_t i = 0; i < controlPoints.size(); i++ ) {

		if ( controlPoints[i].anchorType != ControlPoint::ANCHOR_BOUND ) continue;

		Point2f p0 = newControlPoints[i];
		Point2f p1 = newControlPoints[controlPoints[i].boundNeighbors[0]];
		Point2f p2 = newControlPoints[controlPoints[i].boundNeighbors[1]];

		Point2f u1 = p0 - p1;
		Point2f u2 = p2 - p1;

		double norm = NormL2( u2 );
		if ( SignNumber( norm ) == 0 ) {
			newControlPoints[i] = p1;
			continue;
		}

		double projection = DotProduct( u1, u2 ) / sqr( norm );

		projection = max( min( projection, 1.0 ), 0.0 );
		newControlPoints[i] = p1 + projection * u2;

	}

}

void Deformation::UpdateControlPoints( const vector<Point2f> &newControlPoints ) {

	//#define DEBUG_MIN_ENERGY_UPDATE

	for ( int i = 0; i < controlPointsNum; i++ ) {

		if ( controlPoints[i].anchorType != ControlPoint::ANCHOR_STATIC ) {
#ifdef DEBUG_MIN_ENERGY_UPDATE
			if ( controlPoints[i].frameId == 0 )
				cout << controlPoints[i].pos << " " << newControlPoints[i] << endl;
#endif
			controlPoints[i].pos = newControlPoints[i];

			Point2f hostPos( -1, -1 );
			if ( controlPoints[i].boundNeighbors.size() > 0 ) {
				int hostPointIndex = controlPoints[i].boundNeighbors[0];
				hostPos = controlPoints[hostPointIndex].pos;
			}

			switch ( controlPoints[i].anchorType ) {
				case ControlPoint::ANCHOR_STATIC_LEFT:
					controlPoints[i].pos.x = 0;
					controlPoints[i].pos.y = hostPos.y;
					break;
				case ControlPoint::ANCHOR_STATIC_TOP:
					controlPoints[i].pos.x = hostPos.x;
					controlPoints[i].pos.y = 0;
					break;
				case ControlPoint::ANCHOR_STATIC_RIGHT:
					controlPoints[i].pos.x = deformedFrameSize.width - 1;
					controlPoints[i].pos.y = hostPos.y;
					break;
				case ControlPoint::ANCHOR_STATIC_BOTTOM:
					controlPoints[i].pos.x = hostPos.x;
					controlPoints[i].pos.y = deformedFrameSize.height - 1;
					break;
				case ControlPoint::ANCHOR_BOUND:
				case ControlPoint::ANCHOR_CENTER:
					RestrictInside( controlPoints[i].pos, deformedFrameSize );
					break;
				default:
					break;

			}

		}

	}

}

void Deformation::MinimizeEnergy() {

#define DEBUG_MIN_ENERGY
	
	printf( "\nMinimize deformation energy.\n" );

	double curE = CalcEnergy();
	printf( "\tIter 0. Energy: %.3lf.\n", curE );

	for ( int iter = 0; iter < MIN_ENERGY_ITERS; iter++ ) {

		vector<Point2f> newControlPoints( controlPointsNum );

		OptimizeEnergyFunction( newControlPoints );
		CollinearConstraints( newControlPoints );
		UpdateControlPoints( newControlPoints );

		double preE = curE;
		curE = CalcEnergy();

		printf( "\tIter %d. Energy: %.3lf.\n", iter + 1, curE );

#ifdef DEBUG_MIN_ENERGY
		Mat edgeImg;
		DrawEdge(0, DEFORMED_POS, edgeImg);
		imshow( "Edge Image", edgeImg );
		waitKey(0);
#endif 

	}
}


Point2f Deformation::CalcPointByBaryCoord( const vector<BaryCoord> &baryCoord, int posType ) {

	Point2f deformedPoint( 0, 0 );

	for ( const auto &vertex : baryCoord ) {
		// cout << vertex.first << " " << vertex.second << endl;
		ControlPoint point = controlPoints[vertex.second];
		if ( posType == ORIGIN_POS ) {
#ifdef DEBUG
			// cout << vertex.first << " " << point.originPos << " ";
#endif
			deformedPoint += vertex.first * point.originPos;
		} else {
			deformedPoint += vertex.first * point.pos;
		}
	}

	// cout << endl;

	return deformedPoint;

}

void Deformation::CalcDeformedMap() {

//#define DEBUG_CALC_DEFORMED_MAP

	clock_t timeSt = clock();

	deformedMap.clear();
	
	int controlPointIndex = 0;
	Rect rect( 0, 0, deformedFrameSize.width, deformedFrameSize.height );

	for ( int frameId = 0; frameId < frameNum; frameId++ ) {

		deformedMap.push_back( Mat( deformedFrameSize, CV_32FC2 ) );

		printf( "Calculate key frames deformed map. Progress rate %d/%d.\r", frameId + 1, frameNum );

		Subdiv2D subdiv( rect );
		Mat cpMap = Mat( frameSize, CV_32SC1, Scalar( -1 ) );

		for ( ; controlPointIndex < controlPointsNum; controlPointIndex++ ) {
			
			ControlPoint &controlPoint = controlPoints[controlPointIndex];

			if ( controlPoint.frameId != frameId ) break;

			subdiv.insert( controlPoint.pos );
			cpMap.at<int>( controlPoint.pos ) = controlPointIndex;

		}

		for ( int y = 0; y < deformedFrameSize.height; y++ ) {
			for ( int x = 0; x < deformedFrameSize.width; x++ ) {

				vector<BaryCoord> baryCoord;
				Point2f originPoint, deformedPoint;
				
				deformedPoint = Point2f( x, y );
				int locateStatus = LocatePoint( subdiv, cpMap, deformedPoint, baryCoord );
				if ( locateStatus == CV_PTLOC_INSIDE || locateStatus == CV_PTLOC_ON_EDGE || locateStatus == CV_PTLOC_VERTEX ) {
					originPoint = CalcPointByBaryCoord( baryCoord, ORIGIN_POS );
					RestrictInside( originPoint, frameSize );
					deformedMap[frameId].at<Point2f>( deformedPoint ) = originPoint;
				} else {
					printf( "[CalcDeformedMap] Locate error. " );
					cout << deformedPoint << endl;
				}
				
#ifdef DEBUG_CALC_DEFORMED_MAP
				if ( frameId == 0 && y == 80 && x <= 30 ) {
					for ( size_t i = 0; i < baryCoord.size(); i++ ) {
						cout << baryCoord[i].first << " " << controlPoints[baryCoord[i].second].pos << " " << controlPoints[baryCoord[i].second].originPos << endl;
					}
					cout << x << " " << originPoint << " " << deformedPoint << endl << endl;
				}

#endif		

#ifdef DEBUG
				// DrawLocate( deformedPoint, baryCoord );
#endif
			}
		}

	}

	printf( "\n" );
	clock_t timeEd = clock();
	printf( "Calculate key frames deformed map. Time used %ds.\n", (timeEd - timeSt) / 1000 );

}

void Deformation::RenderFrame( const Mat &img, const Mat &deformedMap, Mat &deformedImg ) {

	Size frameSize = img.size();

	deformedImg = Mat::zeros( deformedFrameSize, CV_8UC3 );

	for ( int y = 0; y < deformedFrameSize.height; y++ ) {
		for ( int x = 0; x < deformedFrameSize.width; x++ ) {

			Point2f originPoint, deformedPoint;
			deformedPoint = Point2f( x, y );
			originPoint = deformedMap.at<Point2f>( deformedPoint );

			deformedImg.at<Vec3b>( deformedPoint ) = img.at<Vec3b>( originPoint );

		}
	}

	imshow( "Deformed Frame", deformedImg );

}

void Deformation::RenderKeyFrames() {

	printf( "Render key frames.\n" );

	for ( int i = 0; i < frameNum; i++ ) {

		Mat deformedFrame, edgeImg;

		RenderFrame( frames[i].img, deformedMap[i], deformedFrame );

		deformedFrames.push_back( deformedFrame );

		//imshow( "Saliency Map", frames[i].saliencyMap );
		//imshow( "Origin Frame", frames[i].img );
		DrawEdge( i, DEFORMED_POS_WITH_FRAME, edgeImg );
		WriteKeyFrameEdgeImg( frames[i].frameId, edgeImg, videoName );

		//waitKey();

	}

}

void Deformation::RenderFrames( const vector<Mat> &_frames, int shotSt, int shotEd ) {

	int keyframeId = 0;

	for ( int i = shotSt; i < shotEd; i++ ) {

		Mat deformedFrame;
		int keyframeIdInSeries = frames[keyframeId].frameId;

		if ( i == keyframeIdInSeries ) {

			deformedFrame = deformedFrames[keyframeId];
			keyframeId++;

		} else if ( i < keyframeIdInSeries ) {

			RenderFrame( _frames[i - shotSt], deformedMap[keyframeId], deformedFrame );

		} else if ( i > keyframeIdInSeries ) {

			cout << "Error" << i << " " << keyframeId << " " << keyframeIdInSeries << endl;

		}

		WriteDeformedImg( i, deformedFrame, videoName );
		
		imshow( "Deformed Frame", deformedFrame );
		waitKey( 1 );


	}

}