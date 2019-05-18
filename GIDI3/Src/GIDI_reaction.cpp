/*
# <<BEGIN-copyright>>
# Copyright 2019, Lawrence Livermore National Security, LLC.
# See the top-level COPYRIGHT file for details.
# 
# SPDX-License-Identifier: MIT
# <<END-copyright>>
*/

#include "GIDI.hpp"

namespace GIDI {

/* *********************************************************************************************************//**
 * Parses a <**reaction**> node.
 ***********************************************************************************************************/

Reaction::Reaction( int a_ENDF_MT, std::string a_fissionGenre ) :
        Form( f_reaction ),
        m_ENDF_MT( a_ENDF_MT ),
        m_fissionGenre( a_fissionGenre ),
        m_doubleDifferentialCrossSection( doubleDifferentialCrossSectionMoniker ),
        m_crossSection( crossSectionMoniker ),
        m_availableEnergy( availableEnergyMoniker ),
        m_availableMomentum( availableMomentumMoniker ),
        m_outputChannel( ) {

    moniker( reactionMoniker );
}

/* *********************************************************************************************************//**
 * Parses a <**reaction**> node.
 *
 * @param a_construction    [in]    Used to pass user options to the constructor.
 * @param a_node            [in]    The reaction pugi::xml_node to be parsed and used to construct the reaction.
 * @param a_pops            [in]    The *external* PoPs::Database instance used to get particle indices and possibly other particle information.
 * @param a_internalPoPs    [in]    The *internal* PoPs::Database instance used to get particle indices and possibly other particle information.
 *                                  This is the <**PoPs**> node under the <**reactionSuite**> node.
 * @param a_protare         [in]    The GIDI::Protare this reaction belongs to.
 * @param a_styles          [in]    The <**styles**> node under the <**reactionSuite**> node.
 ***********************************************************************************************************/

Reaction::Reaction( Construction::Settings const &a_construction, pugi::xml_node const &a_node, PoPs::Database const &a_pops, 
                PoPs::Database const &a_internalPoPs, Protare const &a_protare, Styles::Suite const *a_styles ) :
        Form( a_node, f_reaction ),
        m_ENDF_MT( a_node.attribute( "ENDF_MT" ).as_int( ) ),
        m_ENDL_C( 0 ),
        m_ENDL_S( 0 ),
        m_fissionGenre( a_node.attribute( "fissionGenre" ).value( ) ),
        m_QThreshold( 0.0 ),
        m_crossSectionThreshold( 0.0 ),
        m_doubleDifferentialCrossSection( a_construction, doubleDifferentialCrossSectionMoniker, a_node, a_pops, a_internalPoPs, parseDoubleDifferentialCrossSectionSuite, a_styles ),
        m_crossSection( a_construction, crossSectionMoniker, a_node, a_pops, a_internalPoPs, parseCrossSectionSuite, a_styles ),
        m_availableEnergy( a_construction, availableEnergyMoniker, a_node, a_pops, a_internalPoPs, parseAvailableSuite, a_styles ),
        m_availableMomentum( a_construction, availableMomentumMoniker, a_node, a_pops, a_internalPoPs, parseAvailableSuite, a_styles ),
        m_outputChannel( NULL ) {

    m_isPairProduction = label( ).find( "pair production" ) != std::string::npos;

    m_doubleDifferentialCrossSection.setAncestor( this );
    m_crossSection.setAncestor( this );
    m_availableEnergy.setAncestor( this );
    m_availableMomentum.setAncestor( this );

    ENDL_CFromENDF_MT( m_ENDF_MT, &m_ENDL_C, &m_ENDL_S );

    m_outputChannel = new OutputChannel( a_construction, a_node.child( outputChannelMoniker ), a_pops, a_internalPoPs, a_styles );
    m_outputChannel->setAncestor( this );

    if( ( a_construction.parseMode( ) != Construction::e_outline ) && ( a_construction.parseMode( ) != Construction::e_readOnly ) ) {
        double _Q = 0.0;
        Form const &QForm = **(m_outputChannel->Q( ).begin( ));
        switch( QForm.type( ) ) {
        case f_constant1d : {
            Constant1d const &constant1d = static_cast<Constant1d const &>( QForm );
            _Q = constant1d.value( );
            break; }
        case f_XYs1d : {
            XYs1d const &xys1d = static_cast<XYs1d const &>( QForm );
            _Q = xys1d.evaluate( 0.0 );
            break; }
        default :
            throw std::runtime_error( "Reaction::Reaction: unsupported Q form " + QForm.label( ) );
        }
        _Q *= -1;
        if( _Q <= 0.0 ) _Q = 0.0;
        m_QThreshold = a_protare.thresholdFactor( ) * _Q;

        if( _Q > 0.0 ) {               // Try to do a better job determining m_crossSectionThreshold.
            std::vector<Suite::const_iterator> monikers = a_protare.styles( ).findAllOfMoniker( griddedCrossSectionStyleMoniker );
            if( ( a_construction.parseMode( ) != Construction::e_multiGroupOnly ) && ( monikers.size( ) > 0 ) ) {
                Styles::GriddedCrossSection const &griddedCrossSection = static_cast<Styles::GriddedCrossSection const &>( **monikers[0] );
                Grid grid = griddedCrossSection.grid( );

                Ys1d const &ys1d = static_cast<Ys1d const &>( *m_crossSection.get<Ys1d>( griddedCrossSection.label( ) ) );
                m_crossSectionThreshold = grid[ys1d.start( )]; }
            else {      // Should also check 'evaluate' style before using m_QThreshold as a default.
                m_crossSectionThreshold = m_QThreshold;
            }
        }
    }
}

/* *********************************************************************************************************//**
 ***********************************************************************************************************/

Reaction::~Reaction( ) {

    if( m_outputChannel != NULL ) delete m_outputChannel;
}

/* *********************************************************************************************************//**
 * Fills in a std::set with a unique list of all product indices produced by this reaction.
 * If a_transportablesOnly is true, only transportable product indices are return.
 *
 * @param a_ids                     [out]   The unique list of product indices.
 * @param a_particles               [in]    The list of particles to be transported.
 * @param a_transportablesOnly      [in]    If true, only transportable product indices are added in the list.
 ***********************************************************************************************************/

void Reaction::productIDs( std::set<std::string> &a_ids, Settings::Particles const &a_particles, bool a_transportablesOnly ) const {

    m_outputChannel->productIDs( a_ids, a_particles, a_transportablesOnly );
}

/* *********************************************************************************************************//**
 * Determines the maximum Legredre order present in the multi-group transfer matrix for a give product for a give label. Inspects all
 * products produced by this reaction.
 *
 * @param a_settings        [in]    Specifies the requested label.
 * @param a_productID       [in]    Particle id of the requested product.
 * @return                          The maximum Legredre order. If no transfer matrix data are present for the requested product, -1 is returned.
 ***********************************************************************************************************/

int Reaction::maximumLegendreOrder( Settings::MG const &a_settings, std::string const &a_productID ) const {

    if( m_isPairProduction && ( a_productID == PoPs::IDs::photon ) ) return( 0 );
    return( m_outputChannel->maximumLegendreOrder( a_settings, a_productID ) );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, total multiplicity for the requested label for the requested product. This is a cross section weighted multiplicity.
 *
 * @param a_settings        [in]    Specifies the requested label.
 * @param a_particles       [in]    The list of particles to be transported.
 * @param a_productID       [in]    Particle id for the requested product.
 * @return                          The requested multi-group multiplicity as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupMultiplicity( Settings::MG const &a_settings, Settings::Particles const &a_particles, std::string const &a_productID ) const {

    Vector vector( 0 );

    if( m_isPairProduction ) {
        if( a_productID == PoPs::IDs::photon ) vector += multiGroupCrossSection( a_settings, a_particles ) * 2; }
    else {
        vector += m_outputChannel->multiGroupMultiplicity( a_settings, a_particles, a_productID );
    }

    return( vector );
}

/* *********************************************************************************************************//**
 * Returns true if at least one output channel contains a fission channel.
 *
 * @return  true if at least one output channel is a fission channel.
 ***********************************************************************************************************/

bool Reaction::hasFission( ) const {

    if( m_ENDF_MT == 18 ) return( true );
    if( m_ENDF_MT == 19 ) return( true );
    if( m_ENDF_MT == 20 ) return( true );
    if( m_ENDF_MT == 21 ) return( true );
    if( m_ENDF_MT == 38 ) return( true );

    return( false );
}

/* *********************************************************************************************************//**
 * Sets *this* reaction's output channel to **a_outputChannel**.
 *
 * @param a_outputChannel   [in]    The output channel to make *this* reaction output channel.
 ***********************************************************************************************************/

void Reaction::outputChannel( OutputChannel *a_outputChannel ) {

    m_outputChannel = a_outputChannel;
    m_outputChannel->setAncestor( this );
}

/* *********************************************************************************************************//**
 * Used by Ancestry to tranverse GNDS nodes. This method returns a pointer to a derived class' a_item member or NULL if none exists.
 *
 * @param a_item    [in]    The name of the class member whose pointer is to be return.
 * @return                  The pointer to the class member or NULL if class does not have a member named a_item.
 ***********************************************************************************************************/

Ancestry const *Reaction::findInAncestry3( std::string const &a_item ) const {

    if( a_item == doubleDifferentialCrossSectionMoniker ) return( &m_doubleDifferentialCrossSection );
    if( a_item == crossSectionMoniker ) return( &m_crossSection );
    if( a_item == availableEnergyMoniker ) return( &m_availableEnergy );
    if( a_item == availableMomentumMoniker ) return( &m_availableMomentum );
    if( a_item == outputChannelMoniker ) return( m_outputChannel );

    return( NULL );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, cross section for the requested label the reaction.
 *
 * @param a_settings    [in]    Specifies the requested label.
 * @param a_particles   [in]    The list of particles to be transported.
 * @return                      The requested multi-group cross section as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupCrossSection( Settings::MG const &a_settings, Settings::Particles const &a_particles ) const {

    Vector const &_crossSection = m_crossSection.getViaLineage<Gridded1d>( a_settings.label( ) )->data( );

    return( _crossSection );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, product matrix for the requested label for the requested product index for the requested Legendre order.
 * If no data are found, an empty GIDI::Matrix is returned.
 *
 * @param a_settings        [in]    Specifies the requested label and if delayed neutrons should be included.
 * @param a_particles       [in]    The list of particles to be transported.
 * @param a_productID       [in]    Particle id for the requested product.
 * @param a_order           [in]    Requested product matrix, Legendre order.
 * @return                          The requested multi-group product matrix as a GIDI::Matrix.
 ***********************************************************************************************************/

Matrix Reaction::multiGroupProductMatrix( Settings::MG const &a_settings, Settings::Particles const &a_particles, std::string const &a_productID, int a_order ) const {

    Matrix matrix( 0, 0 );

    if( m_isPairProduction ) {
        if( a_productID == PoPs::IDs::photon ) {
            if( a_order == 0 ) {
                Vector productionCrossSection = multiGroupCrossSection( a_settings, a_particles ) * 2;
                std::map<std::string, GIDI::Settings::Particle> const &particles = a_particles.particles( );
                std::map<std::string, GIDI::Settings::Particle>::const_iterator particle = particles.find( PoPs::IDs::photon );
                GIDI::Settings::MultiGroup const &multiGroup = particle->second.multiGroup( );
                int multiGroupIndexFromEnergy = multiGroup.multiGroupIndexFromEnergy( PoPs_electronMass_MeV_c2, true );
                Matrix matrix2( productionCrossSection.size( ), productionCrossSection.size( ) );

                for( std::size_t i1 = 0; i1 < productionCrossSection.size( ); ++i1 ) {
                    matrix2.set( i1, multiGroupIndexFromEnergy, productionCrossSection[i1] );
                }
                matrix += matrix2;
            }
        } }
    else {
        matrix += m_outputChannel->multiGroupProductMatrix( a_settings, a_particles, a_productID, a_order );
    }

    return( matrix );
}

/* *********************************************************************************************************//**
 * Like Reaction::multiGroupProductMatrix, but only returns the fission neutron, transfer matrix.
 *
 * @param a_settings    [in]    Specifies the requested label and if delayed neutrons should be included.
 * @param a_particles   [in]    The list of particles to be transported.
 * @param a_order       [in]    Requested product matrix, Legendre order.
 * @return                      The requested multi-group neutron fission matrix as a GIDI::Matrix.
 ***********************************************************************************************************/

Matrix Reaction::multiGroupFissionMatrix( Settings::MG const &a_settings, Settings::Particles const &a_particles, int a_order ) const {

    Matrix matrix( 0, 0 );

    if( hasFission( ) ) matrix += multiGroupProductMatrix( a_settings, a_particles, PoPs::IDs::neutron, a_order );
    return( matrix );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, total available energy for the requested label. This is a cross section weighted available energy.
 *
 * @param a_settings    [in]    Specifies the requested label.
 * @param a_particles   [in]    The list of particles to be transported.
 * @return                      The requested multi-group available energy as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupAvailableEnergy( Settings::MG const &a_settings, Settings::Particles const &a_particles ) const {

    return( m_availableEnergy.getViaLineage<Gridded1d>( a_settings.label( ) )->data( ) );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, total average energy for the requested label for the requested product. This is a cross section weighted average energy
 * summed over all products for this reaction.
 *
 * @param a_settings        [in]    Specifies the requested label.
 * @param a_particles       [in]    The list of particles to be transported.
 * @param a_productID       [in]    Particle id for the requested product.
 * @return                          The requested multi-group average energy as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupAverageEnergy( Settings::MG const &a_settings, Settings::Particles const &a_particles, std::string const &a_productID ) const {

    Vector vector( 0 );

    if( m_isPairProduction ) {
        if( a_productID == PoPs::IDs::photon ) vector += multiGroupCrossSection( a_settings, a_particles ) * 2.0 * PoPs_electronMass_MeV_c2; }
    else {
        vector += m_outputChannel->multiGroupAverageEnergy( a_settings, a_particles, a_productID );
    }

    return( vector );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, total deposition energy for the requested label for *this* reaction. This is a cross section weighted 
 * deposition energy. The deposition energy is calculated by subtracting the average energy from each transportable particle
 * from the available energy. The list of transportable particles is specified via the list of particle specified in the *a_settings* argument.
 * This method does not include any photon deposition energy for *this* reaction that is in the **GNDS** orphanProducts node.
 *
 * @param a_settings    [in]    Specifies the requested label and the products that are transported.
 * @param a_particles   [in]    The list of particles to be transported.
 * @return                      The requested multi-group deposition energy as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupDepositionEnergy( Settings::MG const &a_settings, Settings::Particles const &a_particles ) const {

    std::map<std::string, Settings::Particle> const &products = a_particles.particles( );
    Vector vector = multiGroupAvailableEnergy( a_settings, a_particles );

    for( std::map<std::string, Settings::Particle>::const_iterator iter = products.begin( ); iter != products.end( ); ++iter ) {
        vector -= multiGroupAverageEnergy( a_settings, a_particles, iter->first );
    }

    return( vector );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, total available momentum for the requested label. This is a cross section weighted available momentum.
 *
 * @param a_settings    [in]    Specifies the requested label.
 * @param a_particles   [in]    The list of particles to be transported.
 * @return                      The requested multi-group available momentum as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupAvailableMomentum( Settings::MG const &a_settings, Settings::Particles const &a_particles ) const {

    return( m_availableMomentum.getViaLineage<Gridded1d>( a_settings.label( ) )->data( ) );
}
/* *********************************************************************************************************//**
 * Returns the multi-group, total average momentum for the requested label for the requested product. This is a cross section weighted average momentum.
 *
 * @param a_settings        [in]    Specifies the requested label.
 * @param a_particles       [in]    The list of particles to be transported.
 * @param a_productID       [in]    Particle id for the requested product.
 * @return                          The requested multi-group average momentum as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupAverageMomentum( Settings::MG const &a_settings, Settings::Particles const &a_particles, std::string const &a_productID ) const {

    Vector vector( 0 );

    if( !m_isPairProduction ) vector += m_outputChannel->multiGroupAverageMomentum( a_settings, a_particles, a_productID );

    return( vector );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, total deposition momentum for the requested label for *this* reaction. This is a cross section 
 * weighted deposition momentum. The deposition momentum is calculated by subtracting the average momentum from each transportable particle
 * from the available momentum. The list of transportable particles is specified via the list of particle specified in the *a_settings* argument.
 * This method does not include any photon deposition momentum for *this* reaction that is in the **GNDS** orphanProducts node.
 *
 * @param a_settings    [in]    Specifies the requested label.
 * @param a_particles   [in]    The list of particles to be transported.
 * @return                      The requested multi-group deposition momentum as a GIDI::Vector.
 ***********************************************************************************************************/

Vector Reaction::multiGroupDepositionMomentum( Settings::MG const &a_settings, Settings::Particles const &a_particles ) const {

    std::map<std::string, Settings::Particle> const &products = a_particles.particles( );
    Vector vector = multiGroupAvailableMomentum( a_settings, a_particles );

    for( std::map<std::string, Settings::Particle>::const_iterator iter = products.begin( ); iter != products.end( ); ++iter ) {
        vector -= multiGroupAverageMomentum( a_settings, a_particles, iter->first );
    }

    return( vector );
}

/* *********************************************************************************************************//**
 * Returns the multi-group, gain for the requested particle and label. This is a cross section weighted gain summed over all reactions.
 *
 * @param a_settings    [in]    Specifies the requested label.
 * @param a_particles   [in]    The list of particles to be transported.
 * @param a_productID   [in]    The particle PoPs' id for the whose gain is to be calculated.
 *
 * @return                      The requested multi-group gain as a **GIDI::Vector**.
 ***********************************************************************************************************/

Vector Reaction::multiGroupGain( Settings::MG const &a_settings, Settings::Particles const &a_particles, std::string const &a_productID, std::string const &a_projectileID  ) const {

    Vector vector( multiGroupMultiplicity( a_settings, a_particles, a_productID ) );

    if( a_productID == a_projectileID ) vector -= multiGroupCrossSection( a_settings, a_particles );

    return( vector );
}

/* *********************************************************************************************************//**
 * Returns, via arguments, the average energy and momentum, and gain for product with particle id *a_particleID*.
 *
 * @param a_particleID          [in]    The particle id of the product.
 * @param a_energy              [in]    The energy of the projectile.
 * @param a_productEnergy       [in]    The average energy of the product.
 * @param a_productMomentum     [in]    The average momentum of the product.
 * @param a_productGain         [in]    The gain of the product.
 ***********************************************************************************************************/

void Reaction::continuousEnergyProductData( std::string const &a_particleID, double a_energy, double &a_productEnergy, double &a_productMomentum, double &a_productGain ) const {

    a_productEnergy = 0.0;
    a_productMomentum = 0.0;
    a_productGain = 0.0;

    if( ENDF_MT( ) == 516 ) return;             // FIXME, may be something wrong with the way FUDGE converts ENDF to GNDS.

    if( m_outputChannel != NULL ) m_outputChannel->continuousEnergyProductData( a_particleID, a_energy, a_productEnergy, a_productMomentum, a_productGain );
}

/* *********************************************************************************************************//**
 * Fills the argument *a_writeInfo* with the XML lines that represent *this*. Recursively enters each sub-node.
 *
 * @param       a_writeInfo         [in/out]    Instance containing incremental indentation and other information and stores the appended lines.
 * @param       a_indent            [in]        The amount to indent *this* node.
 ***********************************************************************************************************/

void Reaction::toXMLList( WriteInfo &a_writeInfo, std::string const &a_indent ) const {
    
    std::string indent2 = a_writeInfo.incrementalIndent( a_indent );
    std::string attributes;

    attributes += a_writeInfo.addAttribute( "label", label( ) );
    attributes += a_writeInfo.addAttribute( "ENDF_MT", intToString( ENDF_MT( ) ) );
    if( m_fissionGenre != "" ) attributes += a_writeInfo.addAttribute( "fissionGenre", m_fissionGenre );
    a_writeInfo.addNodeStarter( a_indent, moniker( ), attributes );

    m_doubleDifferentialCrossSection.toXMLList( a_writeInfo, indent2 );    
    m_crossSection.toXMLList( a_writeInfo, indent2 );    
    if( m_outputChannel != NULL ) m_outputChannel->toXMLList( a_writeInfo, indent2 );
    m_availableEnergy.toXMLList( a_writeInfo, indent2 );
    m_availableMomentum.toXMLList( a_writeInfo, indent2 );

    a_writeInfo.addNodeEnder( moniker( ) );
}

/* *********************************************************************************************************//**
 * Calculates the ENDL C and S values for a ENDF MT value.
 *
 * @param ENDF_MT   [in]    The ENDF MT value.
 * @param ENDL_C    [out]   The ENDL C value for the ENDF MT value.
 * @param ENDL_S    [out]   The ENDL S value for the ENDF MT value.
 * @return                  Returns 0 if the ENDF MT value is valid and 1 otherwise.
 ***********************************************************************************************************/

int ENDL_CFromENDF_MT( int ENDF_MT, int *ENDL_C, int *ENDL_S ) {

    int MT1_50ToC[] = { 1,   10,  -3,   -4,   -5,    0,    0,    0,    0,  -10,
                       32,    0,   0,    0,    0,   12,   13,   15,   15,   15,
                       15,   26,  36,   33,  -25,    0,  -27,   20,   27,  -30,
                        0,   22,  24,   25,  -35,  -36,   14,   15,    0,    0,
                       29,   16,   0,   17,   34,    0,    0,    0,    0 };
    int MT100_200ToC[] = { -101,   46,   40,   41,   42,   44,   45,   37, -109,    0,
                             18,   48, -113, -114,   19,   39,   47,    0,    0,    0,
                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
                              0, -152, -153, -154,   43, -156, -157,   23,   31, -160,
                           -161, -162, -163, -164, -165, -166, -167, -168, -169, -170,
                           -171, -172, -173, -174, -175, -176, -177, -178, -179, -180,
                           -181, -182, -183, -184, -185, -186, -187, -188,   28, -190,
                           -191, -192,   38, -194, -195, -196, -197, -198, -199, -200 };

    *ENDL_C = 0;
    *ENDL_S = 0;
    if( ENDF_MT <= 0 ) return( 1 );
    if( ENDF_MT > 891 ) return( 1 );
    if( ENDF_MT < 50 ) {
        *ENDL_C = MT1_50ToC[ENDF_MT - 1]; }
    else if( ENDF_MT <= 91 ) {
        *ENDL_C = 11;
        if( ENDF_MT != 91 ) *ENDL_S = 1; }
    else if( ( ENDF_MT > 100 ) && ( ENDF_MT <= 200 ) ) {
        *ENDL_C = MT100_200ToC[ENDF_MT - 101]; }
    else if( ( ENDF_MT == 452 ) || ( ENDF_MT == 455 ) || ( ENDF_MT == 456 ) || ( ENDF_MT == 458 ) ) {
        *ENDL_C = 15;
        if( ENDF_MT == 455 ) *ENDL_S = 7; }
    else if( ( ENDF_MT >= 502 ) and ( ENDF_MT <= 572 ) ) {
        if( ENDF_MT == 502 ) {
            *ENDL_C = 71; }
        else if( ENDF_MT == 504 ) {
            *ENDL_C = 72; }
        else if( ( ENDF_MT >= 515 ) && ( ENDF_MT <= 517 ) ) {
            *ENDL_C = 74; }
        else if( ENDF_MT == 522 ) {
            *ENDL_C = 73; } }
    else if( ENDF_MT >= 600 ) {
        if( ENDF_MT < 650 ) {
            *ENDL_C = 40;
            if( ENDF_MT != 649 ) *ENDL_S = 1; }
        else if( ENDF_MT < 700 ) {
            *ENDL_C = 41;
            if( ENDF_MT != 699 ) *ENDL_S = 1; }
        else if( ENDF_MT < 750 ) {
            *ENDL_C = 42;
            if( ENDF_MT != 749 ) *ENDL_S = 1; }
        else if( ENDF_MT < 800 ) {
            *ENDL_C = 44;
            if( ENDF_MT != 799 ) *ENDL_S = 1; }
        else if( ENDF_MT < 850 ) {
            *ENDL_C = 45;
            if( ENDF_MT != 849 ) *ENDL_S = 1; }
        else if( ( ENDF_MT >= 875 ) && ( ENDF_MT <= 891 ) ) {
            *ENDL_C = 12;
            if( ENDF_MT != 891 ) *ENDL_S = 1;
        }
    }

    return( 0 );
}

}