
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <utility>
#include <vector>

#include "math.hpp"
#include "rateclass_em.hpp"

using std::cerr;
using std::endl;
using std::exp;
using std::log;
using std::make_pair;
using std::pair;
using std::sort;
using std::vector;


void params_json_dump(
        FILE * const file,
        const double lg_L,
        const double aicc,
        const vector< pair< double, double > > & params
        )
{
    fprintf( file, "{\n  \"logl\":     % .3f,\n  \"aicc\":     % .3f,\n  \"rates\":   [ ", lg_L, aicc );
        
    for ( unsigned i = 0; i < params.size(); ++i ) {
        if ( i > 0 )
            fprintf( file, ", %.7f", params[ i ].second );
        else
            fprintf( file, "%.7f", params[ i ].second );
    }

    fprintf( file, " ],\n  \"weights\": [ " );
    
    for ( unsigned i = 0; i < params.size(); ++i ) {
        if ( i > 0 )
            fprintf( file, ", %.7f", params[ i ].first );
        else
            fprintf( file, "%.7f", params[ i ].first );
    }
    
    fprintf( file, " ]\n}\n" );
    fflush( file );
}


inline
double lg_binomial(
        const int cov,
        const int maj,
        const double * lg_params // weight, rate, inv_rate
        )
{
    double rv = lg_params[ 0 ] + maj * lg_params[ 1 ] + ( cov - maj ) * lg_params[ 2 ];
    // cerr << "lg_bin: " << rv << endl;
    return rv;
}


double lg_likelihood(
        double * const pij,
        const vector< pair< int, int > > & data, // [ ( coverage, majority ) ]
        const vector< pair< double, double > > & params, // [ ( weight, rate ) ]
        bool include_constant = false
        )
{
    double lg_params[ 3 * params.size() ], lg_L = 0.0;

    // precompute log-params
    for ( unsigned i = 0; i < params.size(); ++i ) {
        lg_params[ 3 * i + 0 ] = log( params[ i ].first );
        lg_params[ 3 * i + 1 ] = log( params[ i ].second );
        lg_params[ 3 * i + 2 ] = log( 1.0 - params[ i ].second );
    }

    for ( unsigned i = 0; i < data.size(); ++i ) {
        const int cov = data[ i ].first, maj = data[ i ].second;
        double buf[ params.size() ];
        double max = lg_binomial( cov, maj, &lg_params[ 0 ] );
        double sum = 0.0;

        buf[ 0 ] = max;

        for ( unsigned j = 1; j < params.size(); ++j ) {
            buf[ j ] = lg_binomial( cov, maj, &lg_params[ 3 * j ] );
            if ( buf[ j ] > max )
                max = buf[ j ];
        }

        for ( unsigned j = 0; j < params.size(); ++j ) {
            buf[ j ] = exp( buf[ j ] - max );
            sum += buf[ j ];
        }

        for ( unsigned j = 0; j < params.size(); ++j )
            pij[ i * params.size() + j ] = buf[ j ] / sum;

        lg_L += log( sum ) + max;

        if ( include_constant )
            lg_L += lg_choose( cov, maj );
    }

    return lg_L;
}


void update_params(
        const double * const pij,
        const vector< pair< int, int > > & data, // [ ( coverage, majority ) ]
        vector< pair< double, double > > & params // [ ( weight, rate ) ]
        )
{
    for ( unsigned i = 0; i < params.size(); ++i ) {
        double sum = 0.0, sum_cov = 0.0, sum_maj = 0.0;

        for ( unsigned j = 0; j < data.size(); ++j ) {
            double p = pij[ j * params.size() + i ];
            sum += p;
            sum_cov += p * data[ j ].first;
            sum_maj += p * data[ j ].second;
        }

        params[ i ].first = sum / data.size();

        if ( sum_cov == 0.0 )
            params[ i ].second = 1.0;
        else
            params[ i ].second = sum_maj / sum_cov;
    }
}


void initialize_params( vector< pair< double, double > > & params, const int iter )
{
    double sum = 0.0;

    for ( unsigned i = 0; i < params.size(); ++i ) {
        if ( iter >= 10 || i >= params.size() - 1 ) {
            params[ i ].first = rand() / double( RAND_MAX );
            params[ i ].second = rand() / double( RAND_MAX );
        }
        sum += params[ i ].first;
    }

    for ( unsigned i = 0; i < params.size(); ++i )
        params[ i ].first /= sum;
}


double EM(
        const vector< pair< int, int > > & data, // [ ( coverage, majority ) ]
        vector< pair< double, double > > & params // [ ( weight, rate ) ]
        )
{
    double * pij = new double[ data.size() * params.size() ];
    double lg_L;

    if ( params.size() == 1 ) {
        int sum_cov = 0, sum_maj = 0;

        for ( unsigned i = 0; i < data.size(); ++i ) {
            sum_cov += data[ i ].first;
            sum_maj += data[ i ].second;
        }

        params[ 0 ].first = 1.0;

        if ( sum_cov == 0 )
            params[ 0 ].second = 1.0;
        else
            params[ 0 ].second = double( sum_maj ) / double( sum_cov ); // or inverse of this?
    }
    else {
        lg_L = lg_likelihood( pij, data, params );

        for( int i = 0; i < 100; ++i ) {
            double new_lg_L;

            update_params( pij, data, params );
            new_lg_L = lg_likelihood( pij, data, params );

            if ( fabs( lg_L - new_lg_L ) < 1e-8 )
                break;

            lg_L = new_lg_L;
        }
    }

    lg_L = lg_likelihood( pij, data, params, true );

    delete [] pij;

    return lg_L;
}


double _aicc( const int k, const double lg_L, const int n )
{
    return 2.0 * k - 2.0 * lg_L + ( 2.0 * k * ( k + 1 ) / ( n - k - 1 ) );
}


rateclass_t::rateclass_t( const vector< pair< int, int > > & data ) : data( data ) { }


bool rate_cmp( const pair< double, double > & a, const pair< double, double > & b )
{
    if ( a.second <= b.second )
        return true;
    return false;
}


void rateclass_t::operator()(
        double & lg_L,
        double & aicc,
        vector< pair< double, double > > & params,
        const int nrestart
        ) const
{
    params.clear();
    params.push_back( make_pair( 1.0, 0.5 ) );
    lg_L = EM( data, params );
    aicc = _aicc( 1, lg_L, data.size() );

    for ( int i = 2; ; ++i ) {
        double old_lg_L, old_aicc;
        vector< pair< double, double > > old_params = params;

        old_params.push_back( make_pair( 1.0, 0.5 ) );

        initialize_params( old_params, 0 );
        old_lg_L = EM( data, old_params );

        for ( int j = 1; j < nrestart; ++j ) {
            double new_lg_L;
            vector< pair< double, double > > new_params = old_params;

            initialize_params( new_params, j );
            new_lg_L = EM( data, new_params );

            if ( new_lg_L > old_lg_L ) {
                old_lg_L = new_lg_L;
                old_params = new_params;
            }
        }

        old_aicc = _aicc( 2 * i, old_lg_L, data.size() );

        // if our AICc doesn't improve, we're done
        if ( old_aicc >= aicc )
            break;

        aicc = old_aicc;
        lg_L = old_lg_L;
        params = old_params;
    }

    // we've actually rates corresponding to the majority,
    // but we really want the inverse
    for ( unsigned i = 0; i < params.size(); ++i )
        params[ i ].second = 1.0 - params[ i ].second;

    sort( params.begin(), params.end(), rate_cmp );
}
