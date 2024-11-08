//
// Created by vsevolod on 08/01/23.
//

#ifndef SRC_MODEL_MAGNETAR_H
#define SRC_MODEL_MAGNETAR_H

#include "utilitites/pch.h"
#include "utilitites/logger.h"
#include "utilitites/utils.h"
#include "utilitites/interpolators.h"

#include "ejecta/ejecta.h"

class Magnetar_OLD{
    struct Pars{
        size_t iieq = 0;
        bool useGompertz = false;

        double ns_mass = -1;
        double ns_radius = -1;
        double ns_ellipticity = -1;
        double e_bind = -1; // E_bind(self): Binding energy of the NS
        double eos_i = -1;
        double mdot0 = -1;
        double viscous_time = -1;
        double mu = -1; // G cm^3
        double omega_c = -1; // critical freq. (collapse)
        double ns_crit_beta = -1; // initial condition for EOS; betaSh = T/|W| parameter (Gompertz 2014)
        double omega_kep = -1;

        double omega0=-1;
    };
    Vector & m_tb_arr;
    VecVector m_data{}; // container for the solution of the evolution
    std::unique_ptr<logger> p_log;
    std::unique_ptr<Pars> p_pars;
    bool is_mag_pars_set = false;
public:
    bool run_magnetar = false;
    /// RHS pars
    const static int neq = 1;
    std::vector<std::string> vars {  };
    size_t getNeq() const {
        if (!run_magnetar)
            return 0;
        else
            return neq;
    }
    enum Q_SOL { iomega };

    /// All variables
    enum Q {
        // -- dynamics ---
        itb, iOmega, iMdot, iRlc, iRmag, iRcorot, iFastness, iNdip, iNacc, iNgrav, iLdip, iLprop
    };
    std::vector<std::string> m_vnames{
            "tburst", "omega", "mdot", "r_lc", "r_mag", "r_corot", "fastness", "n_dip", "n_acc", "n_grav", "ldip", "lprop"
    };
//    static constexpr size_t NVALS = 1; // number of variables to save
    inline Vector & operator[](unsigned ll){ return m_data[ll]; }
    inline double & operator()(size_t ivn, size_t ir){ return m_data[ivn][ir]; }

    Magnetar_OLD( Vector & t_grid, int loglevel ) : m_tb_arr(t_grid) {
        ///
        p_pars = std::make_unique<Pars>();
        p_log = std::make_unique<logger>(std::cout, std::cerr, loglevel, "Magnetar");
        /// allocate storage for all data

    }

    Vector & getTbGrid() { return m_tb_arr; }
    Vector getTbGrid(size_t every_it) {
        if ((every_it == 1)||(every_it==0)) return m_tb_arr;
        Vector tmp{};
        for (size_t it = 0; it < m_tb_arr.size(); it = it + every_it){
            tmp.push_back(m_tb_arr[it]);
        }
//        Vector tmp2 (tmp.data(), tmp.size());
        return std::move(tmp);
    }

    void setPars(StrDbMap & pars, StrStrMap & opts){
        run_magnetar = getBoolOpt("run_magnetar", opts, AT,p_log, false, true);
        if (!run_magnetar)
            return;
        // *************************************
        m_data.resize(m_vnames.size());
        for (auto & arr : m_data)
            arr.resize( m_tb_arr.size() );
        // **************************************
        double eos_tov_mass = getDoublePar("eos_tov_mass",pars,AT,p_log,-1,true);
        double eos_alpha = getDoublePar("eos_alpha",pars,AT,p_log,-1,true);
        double eos_beta = getDoublePar("eos_beta",pars,AT,p_log,-1,true);
        double eos_i = getDoublePar("eos_i",pars,AT,p_log,-1,true);
        double ns_b = getDoublePar("ns_b",pars,AT,p_log,-1,true);
        double ns_period = getDoublePar("ns_period",pars,AT,p_log,-1,true);
        double ns_crit_beta = getDoublePar("ns_crit_beta",pars,AT,p_log,-1,true);
        double ns_mass = getDoublePar("ns_mass",pars,AT,p_log,-1,true);
        double ns_radius = getDoublePar("ns_radius",pars,AT,p_log,-1,true);
        double ns_ellipticity = getDoublePar("ns_ellipticity",pars,AT,p_log,-1,true);
        double disk_radius = getDoublePar("disk_radius",pars,AT,p_log,-1,true);
        double disk_alpha = getDoublePar("disk_alpha",pars,AT,p_log,-1,true);
        double disk_mass0 = getDoublePar("disk_mass0",pars,AT,p_log,-1,true);
        double disk_aspect_ratio = getDoublePar("disk_aspect_ratio",pars,AT,p_log,-1,true);
        bool useGompertz = getBoolOpt("useGompertz",opts,AT,p_log, false,true);
        // **************************************

        ns_mass *= CGS::solar_m;
        disk_mass0 *= CGS::solar_m;
        eos_tov_mass *= CGS::solar_m;

        double e_bind = 0.6*ns_mass*CGS::c*CGS::c*(CGS::gravconst * ns_mass) / (ns_radius * CGS::c * CGS::c - 0.5*CGS::gravconst*ns_mass); // Lattimer and Prakash (2001)
        if (eos_i < 0){
            (*p_log)(LOG_WARN, AT) << "eos_i is not correct = "<<eos_i<<", computing from mass, preriod, radius\n";
            eos_i = 0.35 * ns_mass*ns_period*ns_radius; // SHOULD BE GIVEN! IF not: magnetar moment of inertia from Gompertz (2014) norm = 2./5
        }

        /// computeSynchrotronEmissivityAbsorptionAnalytic omega0
        double Omega0; // Gompertz et al. 2014, MNRAS 438, 240-250 ; eq. (10)
        if (!std::isfinite(ns_period) or ns_period < 0.)
            Omega0 = std::sqrt( 2. * ns_crit_beta * e_bind / eos_i );
        else
            Omega0 = 2.*CGS::pi/ns_period;
        double P0 = 2*CGS::pi/Omega0;
        /// computeSynchrotronEmissivityAbsorptionAnalytic Tem (dipole spin-down time) eq. (6) of Zhang & Meszaros (2001) [s]
        double time_spindown = (3.*CGS::c*CGS::c*CGS::c*eos_i) / (ns_b*ns_b*std::pow(ns_radius,6.)*Omega0*Omega0 );
        /// spin-down (plateu) luminocity; eq. (8) of Zhang & Meszaros (2001); [ergs/s]
        double L_em0 = (eos_i*Omega0*Omega0) / (2.*time_spindown);
        /// magnetar magnetic moment [G cm^3]
        double mu = ns_b * ns_radius*ns_radius*ns_radius;
        /// keplerian angular freq. [s^-1]
        double OmegaKep = std::sqrt(CGS::gravconst * ns_mass / (ns_radius*ns_radius*ns_radius));//**0.5
        /// computeSynchrotronEmissivityAbsorptionAnalytic viscous timescale (two versions: Gompertz 2014 and Rrayand) [s]
        double viscous_time = -1;
        if (useGompertz){
            viscous_time = disk_radius*disk_radius;
            double disk_cs = 1e7;
            viscous_time /= (3. * disk_alpha * disk_cs * disk_radius);
        } else {
            double h_disk = disk_radius * disk_aspect_ratio;
            double disk_cs = h_disk*OmegaKep*std::pow(ns_radius/disk_radius, 1.5);
            viscous_time = disk_radius*disk_radius / (3. * disk_alpha * disk_cs * h_disk);
        }
        /// evaluate initial mass accretion rate; eq (3) of King and Ritter 1998 [g/s]
        double Mdot0 = disk_mass0/viscous_time;
        /// critical_angular_velocity; Sun, Zhang & Gao (2017); eq. (25); (NS collapse for Omega < Omega_c (P>Pc); assuming ns_mass=const) [s]
        double crit = ns_mass - eos_tov_mass;
        double critical_period = -1;
        double omega_c = -1;
        if (crit > 0){
            critical_period = std::pow((ns_mass - eos_tov_mass)/(eos_alpha * eos_tov_mass),1./eos_beta);
            omega_c = 2.*CGS::pi/critical_period;
        }
        // **************************************
        p_pars->ns_mass = ns_mass;
        p_pars->ns_radius = ns_radius;
        p_pars->ns_ellipticity = ns_ellipticity;
        p_pars->eos_i = eos_i;
        p_pars->mu = mu;
        p_pars->omega_c = omega_c;
        p_pars->mdot0=Mdot0;
        p_pars->omega0=Omega0;
        p_pars->e_bind=e_bind;
        p_pars->ns_crit_beta=ns_crit_beta;
        p_pars->omega_kep=OmegaKep;
        p_pars->viscous_time=viscous_time;

        p_pars->useGompertz=useGompertz;
        // *************************************
        is_mag_pars_set = true;
    }

    void setInitConditions( double * arr, size_t i ) {
        if (p_pars->omega0 < 0){
            (*p_log)(LOG_ERR,AT)<<" omega0 is not set. Cannot set init. conditions\n";
            exit(1);
        }
        arr[i+Q_SOL::iomega] = p_pars->omega0;
    }

    void insertSolution( const double * sol, size_t it, size_t i ){
        m_data[Q::iOmega][it] = sol[i+Q_SOL::iomega];
    }

    inline double dmacc_dt(double t) {
        double mdot = p_pars->mdot0*std::exp(-t/p_pars->viscous_time);
        mdot = mdot < 1.e-10 ? 1.e-10 : mdot; // setting floor value (numerical reason)
        return mdot;
    }
    inline double radius_magnetospheric(double mdot, double r_lc){
        /// computeSynchrotronEmissivityAbsorptionAnalytic magnetospheric radius
        double r_mag = std::pow(p_pars->mu, 4./7.)
                       * std::pow(CGS::gravconst*p_pars->ns_mass, -1./7.)
                       * std::pow(mdot, -2./7.);
        r_mag = r_mag > 0.999*r_lc ? 0.999*r_lc : r_mag; // cannot exceed lc radius
        return r_mag;
    }
    inline double torque_dipol(double omega, double r_lc, double r_mag){
        /// Compute Dipole spindown torque. Eq (8) of Zhang and Meszaros 2001
        double n_dip = 0.;
        if (p_pars->useGompertz){
            // Gompertz uses the disk's alfven radius in the Bucciantini prescription, but it should actually be the alfven radius of the NS wind...
            n_dip = - 2./3. * p_pars->mu*p_pars->mu * std::pow(omega,3.)
                    / (CGS::c*CGS::c*CGS::c) * std::pow(r_lc/r_mag, 3.);
        }
        else{
            // Eq (2) of Bucciantini et al 2006 (not implemented)
            // Standard dipole spindown, no wind or disk
            n_dip = - 1./6. * p_pars->mu*p_pars->mu * std::pow(omega,3.) / (CGS::c*CGS::c*CGS::c);
        }
        n_dip = omega < p_pars->omega_c ? 0. : n_dip; // Check if NS has collapse
        return n_dip ;
    }
    inline double torque_propeller(double omega, double fastness, double r_mag, double mdot){
        double e_rot = 0.5*p_pars->eos_i*omega*omega; // Rotational energy of the NS
        double beta = e_rot / std::abs(p_pars->e_bind); // betaSh = T/|W| parameter (Gompertz 2014)
        /// computeSynchrotronEmissivityAbsorptionAnalytic accretion torque ( Accretion torque, taking into account the propeller model ) Eq (6-7) of Gompertz et al. 2014
        double n_acc = 0.;
        if ((omega > p_pars->omega_c) and (beta < p_pars->ns_crit_beta)){
            // if NS hasn't collapsed and bar-mode instability is still present
            if (r_mag > p_pars->ns_radius)
                n_acc = (1. - fastness) * std::sqrt(CGS::gravconst * p_pars->ns_mass * r_mag) * mdot; // Eq. (6)
            else
                n_acc = ((1. - omega/p_pars->omega_kep) * std::sqrt(CGS::gravconst*p_pars->ns_mass*r_mag) * mdot); // Eq. (7)
        }
        return n_acc ;
    }
    inline double torque_gws(double omega){
        /// computeSynchrotronEmissivityAbsorptionAnalytic Gravitational wave spindown torque (Zhang and Meszaros 2001)
        double n_grav = -32./5. * CGS::gravconst
                        * std::pow(CGS::pi,6.)
                        * p_pars->eos_i*p_pars->eos_i
                        * p_pars->ns_ellipticity*p_pars->ns_ellipticity
                        * std::pow(omega,5.) / std::pow(CGS::c,5.);
        n_grav = omega < 1.e4 ? 0. : n_grav; // TODO ? Why 1e4 is a limiting factor?!
        return n_grav ;
    }

    void evaluateRhs( double * out_Y, size_t i, double x, double const * Y ){
        // *********************************
        double omega = Y[i+Q_SOL::iomega]; /// dOmega/dt
        // *********************************
        double t = x;

        /// Compute accretion rate on a NS (not evolved, analytic)
        double mdot = dmacc_dt(t); // setting floor value (numerical reason)

        /// Compute light cylinder radius (for a given NS rotation)
        double r_lc = CGS::c/omega;

        /// computeSynchrotronEmissivityAbsorptionAnalytic magnetospheric radius
        double r_mag = radius_magnetospheric(mdot, r_lc);

        /// computeSynchrotronEmissivityAbsorptionAnalytic corotation radius (for a given NS mass and spin)
        double r_corot =  std::pow(CGS::gravconst * p_pars->ns_mass / (omega*omega), 1./3.);

        double fastness = std::pow(r_mag / r_corot, 1.5);

//        double e_rot = 0.5*p_pars->eos_i*omega*omega; // Rotational energy of the NS

//        double betaSh = e_rot / std::abs(p_pars->e_bind); // betaSh = T/|W| parameter (Gompertz 2014)

        /// Compute Dipole spindown torque. Eq (8) of Zhang and Meszaros 2001
        double n_dip = torque_dipol(omega, r_lc, r_mag);

        /// computeSynchrotronEmissivityAbsorptionAnalytic accretion torque ( Accretion torque, taking into account the propeller model ) Eq (6-7) of Gompertz et al. 2014
        double n_acc = torque_propeller(omega, fastness, r_mag, mdot);

        /// computeSynchrotronEmissivityAbsorptionAnalytic Gravitational wave spindown torque (Zhang and Meszaros 2001)
        double n_grav = torque_gws(omega);

        /// domega/dt
        double domegadt = (n_dip + n_acc + n_grav)/p_pars->eos_i;

        double dEsddt = n_dip*omega;
        double dEpropdt = n_acc*omega - CGS::gravconst*p_pars->ns_mass*mdot/r_mag;
        // **************************
        out_Y[iOmega] = domegadt;
//        out_Y[idEsddt] = -n_dip*omega;
//        out_Y[idEpropdt] = -n_acc*omega - CGS::gravconst*p_pars->ns_mass*mdot/r_mag;
        // **************************
    }

    /// TODO use one get gammaMinFunc for all
    inline double getLdip(double tburst, const double * Y, size_t i){
        double omega = Y[i+Q_SOL::iomega]; /// dOmega/dt
        double mdot = dmacc_dt(tburst);
        double r_lc = CGS::c/omega;
        double r_mag = radius_magnetospheric(mdot, r_lc);
        /// Compute Dipole spindown torque. Eq (8) of Zhang and Meszaros 2001
        double n_dip = torque_dipol(omega, r_lc, r_mag);
        /// Dipole spindown luminosity
        double ldip = -n_dip*omega;
        return ldip;
    }
    inline double getLprop(double tburst, const double * Y, size_t i){
        double omega = Y[i+Q_SOL::iomega]; /// dOmega/dt
        double mdot = dmacc_dt(tburst);
        double r_lc = CGS::c/omega;
        double r_mag = radius_magnetospheric(mdot, r_lc);
        double r_corot =  std::pow(CGS::gravconst * p_pars->ns_mass / (omega*omega), 1./3.);
        double fastness = std::pow(r_mag / r_corot, 1.5);
        /// computeSynchrotronEmissivityAbsorptionAnalytic accretion torque ( Accretion torque, taking into account the propeller model ) Eq (6-7) of Gompertz et al. 2014
        double n_acc = torque_propeller(omega, fastness, r_mag, mdot);
        /// propeller luminocity (Gompertz et al. (2014))
        double lprop = - n_acc*omega - CGS::gravconst*p_pars->ns_mass*mdot/r_mag;
        return lprop;
    }
    double getValInt(Q vname, double tb){
        if (tb < m_tb_arr[0] or tb > m_tb_arr[m_tb_arr.size()-1])
            return 0.;
        if (tb == m_tb_arr[0])
            return m_data[vname][0];
        if (tb == m_tb_arr[m_tb_arr.size()-1])
            return m_data[vname][m_tb_arr.size()-1];
        /// interpolate value
        size_t ia = findIndex(tb, m_tb_arr, m_tb_arr.size());
        size_t ib = ia + 1;
        /// interpolate the time in comobing frame that corresponds to the t_obs in observer frame
//        double val = interpSegLin(ia, ib, tb, m_data[vname], m_mag_time);
        double val = interpSegLog(ia, ib, tb, m_data[vname], m_tb_arr);
        return val;
    }

    void addOtherVariables( size_t it ){
        double mdot = dmacc_dt(m_tb_arr[it]);
        double omega = m_data[Q::iOmega][it];
        double r_lc = CGS::c/omega;
        double r_mag = radius_magnetospheric(mdot, r_lc);
        double r_corot =  std::pow(CGS::gravconst * p_pars->ns_mass / (omega*omega), 1./3.);
        double fastness = std::pow(r_mag / r_corot, 1.5);
        /// Compute Dipole spindown torque. Eq (8) of Zhang and Meszaros 2001
        double n_dip = torque_dipol(omega, r_lc, r_mag);
        /// computeSynchrotronEmissivityAbsorptionAnalytic accretion torque ( Accretion torque, taking into account the propeller model ) Eq (6-7) of Gompertz et al. 2014
        double n_acc = torque_propeller(omega, fastness, r_mag, mdot);
        /// computeSynchrotronEmissivityAbsorptionAnalytic Gravitational wave spindown torque (Zhang and Meszaros 2001)
        double n_grav = torque_gws(omega);
        /// Dipole spindown luminosity
        double ldip = -n_dip*omega;
        /// propeller luminocity (Gompertz et al. (2014))
        double lprop = - n_acc*omega - CGS::gravconst*p_pars->ns_mass*mdot/r_mag;
        // ************************
        m_data[Q::itb][it] = m_tb_arr[it];
        m_data[Q::iMdot][it] = mdot;
        m_data[Q::iRlc][it] = r_lc;
        m_data[Q::iRmag][it] = r_mag;
        m_data[Q::iRcorot][it] = r_corot;
        m_data[Q::iFastness][iFastness] = fastness;
        m_data[Q::iNdip][it] = n_dip;
        m_data[Q::iNacc][it] = n_acc;
        m_data[Q::iNgrav][it] = n_grav;
        m_data[Q::iLdip][it] = ldip;
        m_data[Q::iLprop][it] = lprop;
        // ************************
    }

    bool isSolutionOk( double * sol, size_t i ){
        bool is_ok = true;
        if (sol[i+Q_SOL::iomega] < 0){
            (*p_log)(LOG_ERR,AT)<<" fatal. Wrong omega="<<sol[i+Q_SOL::iomega]<<"\n";
            is_ok = false;
        }
        return is_ok;
    }

    void applyUnits( double * sol, size_t i ){

    }
};

/// ----------- Read H5 file with magnetar table -------
class ReadMagnetarEvolutionFile{
    std::unique_ptr<logger> p_log;
    LoadH5 m_ldata;
    size_t data_size = 0; // track the size of arrays to avoid mismatching
public:
    ReadMagnetarEvolutionFile(std::string fapth, int loglevel) {
        p_log = std::make_unique<logger>(std::cout, std::cerr, loglevel, "ReadMagnetarEvolutionFile");
//        auto path_to_table = pars.m_path_to_ejecta_id;
//        path_to_table = "../../tst/dynamics/corr_vel_inf_theta.h5";
        if (!std::experimental::filesystem::exists(fapth))
            throw std::runtime_error("File not found. " + fapth);
        /// loading the datafile
        m_ldata.setFileName(fapth);
        (*p_log)(LOG_INFO, AT) << "Ejecta ID loaded\n";
    }

    Vector get(std::string varname) {
        m_ldata.setVarName(varname);
        Vector data = m_ldata.getData();
        if (data_size == 0)
            data_size =  m_ldata.getSize();
        if (m_ldata.getSize() != data_size){
            (*p_log)(LOG_ERR,AT)<<"Input data size mismatch. All arrays should be the same size."
                                <<" Given array v_n="<<varname<<" has size="<<m_ldata.getSize()
                                <<" Expected size="<<data_size<<"\n";
            exit(1);
        }
        else
            data_size = m_ldata.getSize();
        return std::move(data);
    }
};

namespace MAG{
    /// All variables
    enum Q {
        // -- dynamics ---
        itb, iOmega, ildip, ilacc

    };
    std::vector<std::string> m_vnames{
            "tburst", "omega", "ildip", "ilacc"
    };
}

class Magnetar{
    struct Pars{

    };
    Vector m_mag_time;
    VecVector m_data{}; // container for the solution of the evolution
    std::unique_ptr<logger> p_log;
    std::unique_ptr<Pars> p_pars;
    bool is_mag_pars_set = false;
    Interp1d::METHODS mth;
    int m_loglevel{};
public:
    StrDbMap mag_pars; StrStrMap mag_opts;
    bool run_magnetar = false, load_magnetar = false, save_magnetar = false;
    /// RHS pars
    const static int neq = 1;
    std::vector<std::string> vars {  };
    size_t getNeq() const {
        if (!run_magnetar)
            return 0;
        else
            return neq;
    }
    enum Q_SOL {
        iomega,
    };


//    static constexpr size_t NVALS = 1; // number of variables to save
    inline Vector & operator[](unsigned ll){ return m_data[ll]; }
    inline double & operator()(size_t ivn, size_t ir){ return m_data[ivn][ir]; }

    Magnetar( int loglevel ){// : m_mag_time(t_grid) {
        m_loglevel = loglevel;
        p_pars = std::make_unique<Pars>();
        p_log = std::make_unique<logger>(std::cout, std::cerr, loglevel, "Magnetar");
    }

    Vector & getTbGrid() { return m_mag_time; }
    Vector getTbGrid(size_t every_it) {
        if ((every_it == 1)||(every_it==0)) return m_mag_time;
        Vector tmp{};
        for (size_t it = 0; it < m_mag_time.size(); it = it + every_it){
            tmp.push_back(m_mag_time[it]);
        }
//        Vector tmp2 (tmp.data(), tmp.size());
        return std::move(tmp);
    }
    double getMagValInt(MAG::Q vname, double tb){
        if (!std::isfinite(tb)){
            (*p_log)(LOG_ERR,AT)<<" nan in tb!"<<"\n";
            exit(1);
        }
//        std::cerr << m_data[0][0] << " " << m_data[1][0] << " " << m_data[2][0] << " " << m_data[3][0] << "\n";
        if (!is_mag_pars_set || !load_magnetar){
            (*p_log)(LOG_ERR,AT) << " magnetar is not set/loaded\n";
            exit(1);
        }
        if (tb < m_mag_time[0] or tb > m_mag_time[m_mag_time.size() - 1])
            return 0.;
        if (tb == m_mag_time[0])
            return m_data[vname][0];
        if (tb == m_mag_time[m_mag_time.size() - 1])
            return m_data[vname][m_mag_time.size() - 1];
        /// interpolate value
        size_t ia = findIndex(tb, m_mag_time, m_mag_time.size());
        size_t ib = ia + 1;
        /// interpolate the time in comobing frame that corresponds to the t_obs in observer frame
//        double val = interpSegLin(ia, ib, tb, m_data[vname], m_mag_time);
        if ((m_data[vname][ia] == 0.) || (m_data[vname][ib]==0.))
            return 0.;
        double val = interpSegLog(ia, ib, tb, m_mag_time, m_data[vname]);
        if (!std::isfinite(val)){
            (*p_log)(LOG_ERR,AT)
                << " nan in interpolated value; vname="<<vname<<" tb="<<tb
                << " ia="<<ia<<" ib="<<ib<<" m_data["<<MAG::m_vnames[vname]<<"]["<<ia<<"]="<<m_data[vname][ia]
                << " m_data["<<MAG::m_vnames[vname]<<"]["<<ib<<"]="<<m_data[vname][ib]
                << "\n";
            std::cout << m_mag_time << "\n";
            std::cout << m_data[vname] << "\n";
            exit(1);
        }
        return val;
    }

    /// --------- LOAD MAGNETAR -------------

    void loadMagnetarEvolution(MAG::Q vname, Vector values){
        load_magnetar = true;
        // *************************************
        if ((m_mag_time.size() == 0) || (m_mag_time.size() != values.size()))
            m_mag_time.resize(values.size(), 0.0 );
        // *************************************
        if (m_data.empty()) {
            m_data.resize(MAG::m_vnames.size());
            for (auto &arr: m_data)
                arr.resize(m_mag_time.size());
        }
        // **************************************
        mth = InterpBase::select("linear",p_log);
        // **************************************
        if (vname == MAG::Q::itb)
            m_mag_time = values;
//            vecToArr(values, m_mag_time);
        else
            m_data[vname] = values;
//            vecToArr(values,m_data[vname]);
        is_mag_pars_set = true;
//        std::cerr << m_data[0][0] << " " << m_data[1][0] << " " << m_data[2][0] << " " << m_data[3][0] << "\n";
    }

    /// ------- EVOLVE MAGNETAR -------------
    void setPars(StrDbMap & pars, StrStrMap & opts,
                 std::string working_dir, std::string parfilename, Vector ej_tarr){
        mag_pars = pars;
        mag_opts = opts;

        if ((!mag_pars.empty()) || (!mag_opts.empty())) {
            run_magnetar = getBoolOpt("run_magnetar", mag_opts, AT, p_log, false, true);
            load_magnetar = getBoolOpt("load_magnetar", mag_opts, AT, p_log, false, true);
            save_magnetar = getBoolOpt("save_magnetar", mag_opts, AT, p_log, false, true);
            if (load_magnetar && run_magnetar) {
                (*p_log)(LOG_ERR, AT) << "Cannot run and load magnetar evolution at the same time. Chose one.\n";
                exit(1);
            }
            if (load_magnetar) {
                std::string fname_magnetar_ev = getStrOpt("fname_magnetar_evol", mag_opts, AT, p_log, "", true);
                if (!std::experimental::filesystem::exists(working_dir + fname_magnetar_ev)) {
                    (*p_log)(LOG_ERR, AT) << " File not found. " + working_dir + fname_magnetar_ev << "\n";
                    exit(1);
                }
                /// load the magnetar evolution h5 file and parse the key quantity to magnetar class
                ReadMagnetarEvolutionFile dfile(working_dir + fname_magnetar_ev, m_loglevel);
                loadMagnetarEvolution(MAG::Q::itb, dfile.get("time"));
                loadMagnetarEvolution(MAG::Q::ildip, dfile.get("ldip"));
                loadMagnetarEvolution(MAG::Q::ilacc, dfile.get("lacc"));
                /// check if time grids mismatch
                if (getTbGrid()[0] > ej_tarr[0]) {
                    (*p_log)(LOG_ERR, AT)
                            << "Magnetar timegrid is starts too late. Loaded magnetar times[0] > the one set in parfile "
                            << "(" << getTbGrid()[0] << " > " << ej_tarr[0] << ")\n";
                    exit(1);
                }
            }
            if (run_magnetar) {
                /// pass the t_array manually as when loaded, the time grid may be different
//                setPars(mag_pars, mag_opts, ej_tarr);

            }
        }
        else {
            (*p_log)(LOG_INFO, AT) << "Magnetar is not initialized and will not be considered.\n";
        }
    }
#if 0
    void setPars(StrDbMap & pars, StrStrMap & opts){

        run_magnetar = getBoolOpt("run_magnetar", opts, AT,p_log, false, true);
        load_magnetar = getBoolOpt("load_magnetar", opts, AT,p_log, false, true);

        if (!run_magnetar)
            return;
        m_mag_time = t_arr; // TODO May Not Work. But mangetar needs its own time array...
        // *************************************
        m_data.resize(m_vnames.size());
        for (auto & arr : m_data)
            arr.resizeEachImage( m_mag_time.size() );
        // **************************************
        double ns_mass = getDoublePar("NS_mass0",pars,AT,p_log,-1,true);
        ns_mass *= CGS::solar_m; // Msun -> g
        double ns_radius = getDoublePar("NS_radius0",pars,AT,p_log,-1,true);
        ns_radius *= (1000. * 100); // km -> cm
        double ns_inertia = getDoublePar("NS_I0",pars,AT,p_log,
                                         (2./5.*ns_mass*std::pow(ns_radius,3.0)),false);
        double ns_eps = getDoublePar("NS_eps0",pars,AT,p_log, -1,true);
        double ns_crit_beta = getDoublePar("NS_crit_beta",pars,AT,p_log, -1,true);
        double ns_B0 = getDoublePar("NS_B0",pars,AT,p_log, -1,true);
        double ns_alpha0 = getDoublePar("NS_alpha0",pars,AT,p_log, -1,true);
        double ns_sigma = getDoublePar("NS_sigma_cond",pars,AT,p_log, -1,true);
        double ns_char_length = getDoublePar("NS_char_length",pars,AT,p_log, -1,true);
        double ns_char_edens = getDoublePar("NS_char_edens",pars,AT,p_log, -1,true);
        /// Auxiliary quantity betaSh as defined in eq. (72) of Pons & Vigano (2019).
        double beta_ax = 1.0 / 4.0 * std::pow(ns_radius, 6.) / (ns_inertia * CGS::c*CGS::c*CGS::c);
        /// Dimensionless coefficients k_0, k_1, k_2 for a force-free magnetosphere
        /// taken from Spitkovsky (2006) and Philippov et al. (2014).
        /// For comparison, in vacuum k_0 = 0 and k_1 = k_2 = 2/3.
        Vector k_coefficients{1.0, 1.0, 1.0};
        /// Eddington luminosity assuming Thompson scattering in [erg / s].
        double eddigton_lum = 4.0 * CGS::pi * CGS::gravconst * ns_mass * CGS::mp * CGS::c / CGS::sigmaT;
        // ----
        double disk_circ_radius = getDoublePar("Disk_circ_R0",pars,AT,p_log,-1,true);
        double disk_cent_temp = getDoublePar("Disk_cent_T0",pars,AT,p_log,-1,true);
        double disk_radius0 = getDoublePar("Disk_R0",pars,AT,p_log,-1,true);
        double disk_alpha = getDoublePar("Disk_alpha",pars,AT,p_log,-1,true);
        double disk_cs = getDoublePar("Disk_cs",pars,AT,p_log,-1,true);
        double disk_aspect0 = getDoublePar("Disk_H/R0",pars,AT,p_log,-1,true);
        disk_circ_radius *= (1000. * 100); // rm -> cm
        double disk_mdot0 = getDoublePar("Disk_mdot0",pars,AT,p_log,-1,true);
        double disk_mass0 = getDoublePar("Disk_mass0",pars,AT,p_log,-1,true);
        /// --- For Collapse Criterion
//        double eos_tov_mass = getDoublePar("eos_tov_mass",pars,AT,p_log,-1,true);
//        double eos_alpha = getDoublePar("eos_alpha",pars,AT,p_log,-1,true);
//        double eos_beta = getDoublePar("eos_beta",pars,AT,p_log,-1,true);


        std::string opt;
        /// ----
        double t_v;
        opt = "method_tvis";
        std::string method_tvis = getStrOpt(opt,opts,AT,p_log,"none",true);
        if (method_tvis=="menou"){
            /// typical initial viscousity timescale of the disk; see Menou et al. 2001.
            t_v = 2080.0
                  * std::pow(disk_cent_temp / 1.0e6,-1.0)
                  * std::pow(disk_circ_radius / std::pow(10, 8.),1.0 / 2.0); // s
        }
        else if (method_tvis == "gompertz"){
            /// prescription used in Gompertz 2014
            t_v = disk_radius0 / (3. * disk_alpha * disk_cs * disk_radius0); // s
        }
        else if (method_tvis == "rrayand"){
            double disk_H = disk_radius0 * disk_aspect0;
            double disk_omega_kep = std::sqrt(CGS::gravconst * ns_mass / std::pow(ns_radius,3.));
            if (disk_cs < 0)
                disk_cs = disk_H * disk_omega_kep * std::pow(ns_radius/disk_radius0,1.5);
            t_v = disk_radius0*disk_radius0 / (3. * disk_alpha * disk_cs * disk_H); // s
        }
        else{
            (*p_log)(LOG_ERR,AT)<<" option="<<opt<<" is not recognized."
                                << "Use: "<< "menou" << " , " << "gompertz" << " , "<< "rrayand" << "\n";
            exit(1);
        }
        /// ----
        double mdot0;
        opt = "method_mdot0";
        std::string method_mdot = getStrOpt(opt,opts,AT,p_log,"none",true);
        if (method_mdot=="given"){
            /// typical initial viscousity timescale of the disk; see Menou et al. 2001.
            mdot0 = getDoublePar("Disk_mdot0",pars,AT,p_log,-1, true);
        }
        else if (method_mdot == "disk0"){
            /// prescription used in Gompertz 2014
            mdot0 = disk_mass0 / t_v;
        }
        else{
            (*p_log)(LOG_ERR,AT)<<" option="<<opt<<" is not recognized."
                                << "Use: "<< "given" << " , " << "disk0" << "\n";
            exit(1);
        }
        /// ----
        opt = "method_disk_acc";
        double disk_acc_pl = -1;
        double disk_acc_exp = -1;
        std::string method_disk_acc = getStrOpt(opt,opts,AT,p_log,"none",true);
        if (method_mdot=="pl"){
            /// Power-law index for the disk accretion rate decay.
            disk_acc_pl = getDoublePar("Disk_acc_pl",pars,AT,p_log,-1,true);
        }
        else if (method_mdot == "exp"){
            /// Power-law index for the disk outer radius evolution.
            disk_acc_exp = getDoublePar("Disk_acc_exp",pars,AT,p_log,-1,true);
        }
        else{
            (*p_log)(LOG_ERR,AT)<<" option="<<opt<<" is not recognized."
                                << "Use: "<< "pl" << " , " << "exp" << "\n";
            exit(1);
        }
        /// ----
        double ns_P0;
        opt = "method_P0";
        if (method_mdot=="given"){
            /// Power-law index for the disk accretion rate decay.
            ns_P0 = getDoublePar("NS_P0",pars,AT,p_log,-1,true);
        }
        else if (method_mdot == "crit"){
            /// NS_critical_beta = 0.27 # bar-mode instability criterion
            double OmegaCrit = std::sqrt(2.*ns_crit_beta
                                         * MEQS::e_bind(ns_mass,ns_radius)/ns_inertia);
            ns_P0 = 2. * CGS::pi * OmegaCrit;
        }
        else{
            (*p_log)(LOG_ERR,AT)<<" option="<<opt<<" is not recognized."
                                << "Use: "<< "pl" << " , " << "exp" << "\n";
            exit(1);
        }
        // *************************************

        // *************************************
        is_mag_pars_set = true;
    }
#endif
    void setInitConditions( double * arr, size_t i ) {
    }

    void insertSolution( const double * sol, size_t it, size_t i ){

    }
    void evaluateRhsMag( double * out_Y, size_t i, double x, double const * Y ){

    }
    void evaluateRhsPWN( double * out_Y, size_t i, double x, double const * Y ){

    }

    void addOtherVariables( size_t it ){

    }

    bool isSolutionOk( double * sol, double x, size_t ix, size_t i ){
        return true;
    }

    void applyUnits( double * sol, size_t i ){

    }

    void saveMagnetarEvolution(){
        if (!run_magnetar)
            return;

        (*p_log)(LOG_ERR,AT) << "Not implemented\n";
        exit(1);

//        if (every_it < 1){
//            std::cerr << " every_it must be > 1; Given every_it="<<every_it<<"\n Exiting...";
//            std::cerr << AT << "\n";
//            exit(1);
//        }

//        auto & magnetar = p_mag;
//
//        std::vector<std::vector<double>>  tot_mag_out{};
//        std::vector<std::string> tot_names {};
//        std::unordered_map<std::string,double> group_attrs{};
//
//        auto & mag_v_ns = magnetar->m_vnames;
//        auto t_arr = magnetar->getTbGrid(every_it);
//
//        tot_mag_out.resizeEachImage( mag_v_ns.size() );
//        for (size_t ivar = 0; ivar < mag_v_ns.size(); ivar++){
//            for (size_t it = 0; it < magnetar->getTbGrid().size(); it = it + every_it)
//                tot_mag_out[ivar].emplace_back( (*magnetar)[ static_cast<Magnetar::Q>(ivar) ][it] );
//        }
//
//        std::unordered_map<std::string,double> attrs {};
//
//        p_out->VectorOfVectorsH5(tot_mag_out, mag_v_ns, workingdir+fname, attrs);
    }

};

namespace PWN{
    /// All variables
    enum Q {
        // -- dynamics ---
        itburst, itt, iR, imom, itheta, iGamma, ibeta, iEnb, iEpwn, iB, iGammaTermShock, idr

    };
    std::vector<std::string> m_vnames{
            "tburst", "tt", "Rw", "mom", "theta", "Gamma", "betaSh", "Enebula", "Epwn", "B", "GammaTermShock", "dr"
    };
}

double nu_from_gamma(double b_pwn, double gamma_i){
    return 3./(4.*M_PI)*gamma_i*gamma_i*(CGS::ELEC*b_pwn)/(CGS::M_ELEC*CGS::c);
}
double pwn_spectrum(double nu, double r_pwn, double b_pwn, double n_ext, double tcomov, double lum_mag, double eps_e, double gamma_b){
    /// From 2.2 Sec. Emission from the PWN in MNRAS 512, 5572–5579 (2022) https://doi.org/10.1093/mnras/stac797
    double q1 = 1.5;
    double q2 = 2.5;

//    double eps_e = 0.1;
    double gamma_m = 3.;

//    double gamma_b = 1e4; // ????
    double gamma_c = 6.*M_PI*CGS::M_ELEC*CGS::c/(SIGMA_T*b_pwn*b_pwn*tcomov);
    double gamma_M = std::sqrt( 9.*CGS::M_ELEC*CGS::M_ELEC*CGS::c*CGS::c*CGS::c*CGS::c/(8.*b_pwn*CGS::ELEC*CGS::ELEC*CGS::ELEC) );
    ///
    double nu_m = nu_from_gamma(b_pwn, gamma_m);
    double nu_c = nu_from_gamma(b_pwn, gamma_c);
    double nu_M = nu_from_gamma(b_pwn, gamma_M);
    double nu_b = nu_from_gamma(b_pwn, gamma_b);
    double nu_a = 0.;

    double xi_q1 = 5./3.;
    double n_pwn = lum_mag / (4.*M_PI*r_pwn*r_pwn*gamma_b*CGS::M_ELEC*CGS::c*CGS::c*CGS::c);

    double Rb = 1. / (2. - q1) + 1. / (q2 - 2.);
    double eta = std::min(1., std::pow(nu_b / nu_c, (q2 - 2.) / 2.));
    double xi = eta * eps_e;
    double prefac = xi * lum_mag / (2. * Rb);

//    std::vector<double> nu_arr = MakeLogspaceVec(8.,22.,100.,10);
//    std::vector<double> spec (nu_arr.size());
    /// find nu_a from optical depth = 1
    double tau_a = 1.;
//        xi_q1 * ELEC * n_ext * r_pwn / b_pwn / std::pow(gamma_m,5.) * std::pow(nu / nu_m, -q1/2. - 2.);
    nu_a = nu_m * std::pow(tau_a * xi_q1 * CGS::ELEC * (n_pwn) * r_pwn / b_pwn / std::pow(gamma_m,5.)
                           * std::pow(nu / nu_m, -q1/2. - 2.), q1/2.+2.);

    double spec = 0.;
    if (nu_c < nu_b){
        /// fast coocling
        if ((nu_m <= nu)&&(nu <= nu_a))
            spec = std::pow(nu_c/nu_b, (2.-q1)/2.)
                      * std::pow(nu_a/nu_c, (3.-q1)/2.)
                      * std::pow(nu/nu_a, 5./2.);
        else if ((nu_a <= nu)&&(nu<=nu_c))
            spec = std::pow(nu_c/nu_b, (2.-q1)/2.)
                      * std::pow(nu/nu_c,(3.-q1)/2.);
        else if ((nu_c<=nu)&&(nu<=nu_b))
            spec = std::pow(nu/nu_b,(2.-q1)/2.);
        else if ((nu_b<=nu)&&(nu<=nu_M))
            spec = std::pow(nu/nu_b, (2.-q2)/2.);
        else{
            std::cerr << " error in spectrum \n"; exit(1);
        }
    }
    else if (nu_c >= nu_b){
        if((nu_m<=nu)&&(nu<=nu_a))
            spec = std::pow(nu_b/nu_c,(3.-q2)/2.)
                      * std::pow(nu_a/nu_b,(3.-q1)/2.)
                      * std::pow(nu/nu_a,5./2.);
        else if ((nu_a<=nu)&&(nu<=nu_b))
            spec = std::pow(nu_b/nu_c,(3.-q2)/2.)
                      * std::pow(nu/nu_b,(3.-q1)/2.);
        else if ((nu_b<=nu)&&(nu<=nu_c))
            spec = std::pow(nu/nu_c,(3.-q2)/2.);
        else if ((nu_c<=nu)&&(nu<=nu_M))
            spec = std::pow(nu/nu_c,(2.-q2)/2.);
        else{
            std::cerr << " error in spectrum \n"; exit(1);
        }
    }
    double fnu = prefac * spec;
//    print_xy_as_numpy(nu_arr,spec,spec.size(),1);
    return fnu;
}

struct PWNPars{
    PWNPars(std::unique_ptr<Magnetar> & pp_mag,
            std::unique_ptr<Ejecta> & pp_ej,
            VecVector & data, unsigned loglevel)
            : p_mag(pp_mag), p_ej(pp_ej), m_data(data) {
        p_log = std::make_unique<logger>(std::cout,std::cerr,loglevel,"PWN PWNPars");
    }
    std::unique_ptr<Ejecta> & p_ej;
    std::unique_ptr<Magnetar> & p_mag;
    std::unique_ptr<MagnetarSynchrotron> p_syn = nullptr;
//        std::unique_ptr<SynchrotronAnalytic> p_mphys = nullptr;
    std::unique_ptr<logger> p_log;
    Vector m_freq_arr{}; Vector m_time_arr{}; Vector m_r_arr{};
    Vector m_synch_em{}; Vector m_synch_abs{}; Vector m_spectrum{};
    VecVector & m_data;
    double ctheta0=-1.;
    // --------------
    size_t iieq{};
    double Rw0{};
    double mom={};
//        double vel_w0{};
    // ---------------
    double eps_e{};
    double eps_mag{};
    double eps_th{};
    // --------------
    double rho_ej_curr{};
    double tau_ej_curr{};
    double r_ej_curr{};
    double v_ej_curr{};
//        double temp_ej_curr{};
    // -------------
    double curr_ldip{};
    double curr_lacc{};
    // -------------
    double albd_fac{};
    double gamma_b{};
    int iterations{};
    int nthreads_for_frac{};
    // --------------
    double b_pwn=-1;
    bool is_init = false;
    bool do_opt_depth_ej_calc = false;
    // -------------
    double d_l=-1.;
    double theta_obs=-1.;
    double z=-1.;
    // ------------
    size_t ilayer = 0;
    size_t ishell = 1;
    size_t ncells=0;
    // -----
    int tmp_ia_r=0;
    int tmp_ia_nu=0;

};

class PWNmodel{
    CommonTables & commonTables;
//    Vector m_tb_arr;
    VecVector m_data{}; // container for the solution of the evolution
    std::unique_ptr<logger> p_log;
//    std::unique_ptr<PWNPars> p_pars;
    PWNPars * p_pars = nullptr;
    std::unique_ptr<EATS> p_eats;
    Vector frac_psr_dep_{};
//    size_t m_ilayer=0;size_t m_ishell=0;size_t m_ncells=0;
    std::unique_ptr<Ejecta> & p_ej;
    std::unique_ptr<Magnetar> & p_mag;
    unsigned m_loglevel{};
public:
    size_t i_end_r = 0;
    bool run_pwn = false; bool load_pwn = false;
    /// RHS pars
    const static int neq = 5;
    std::vector<std::string> vars {  };
    size_t getNeq() const {
        if (run_pwn)
            return neq;
        else
            return 0;
    }
    enum Q_SOL {
        i_Rw, // Wind radius (PWN radius)
//        i_mom,
        i_tt,
        i_Enb, // PWN total_rad energy
        i_Epwn
    };

//    static constexpr size_t NVALS = 1; // number of variables to save
    inline Vector & operator[](unsigned ll){ return m_data[ll]; }
    inline double & operator()(size_t ivn, size_t ir){ return m_data[ivn][ir]; }

    PWNmodel( //Vector & tarr, double ctheta0, size_t ilayer, size_t ishell, size_t ncells, int loglevel,
            std::unique_ptr<Magnetar> & pp_mag,
            std::unique_ptr<Ejecta> & pp_ej,
            size_t ilayer, CommonTables & commonTables, bool allocate)
            : p_mag(pp_mag), p_ej(pp_ej), commonTables(commonTables) {// : m_mag_time(t_grid) {
        m_loglevel = p_ej->getShells()[0]->getPars()->m_loglevel;
        p_log = std::make_unique<logger>(std::cout, std::cerr, m_loglevel, "PWN");
        // ------------
        m_data.resize(PWN::m_vnames.size());
        size_t n_tb = p_ej->getShells()[0]->getBWs()[0]->get_tburst().size();
        for (auto & arr : m_data)
            if (allocate)
                arr.resize(n_tb, 0.0);
        p_pars = new PWNPars(pp_mag, pp_ej, m_data, m_loglevel);//std::make_unique<PWNPars>();
        p_pars->ilayer = ilayer;
        p_pars->ishell = 0;
        auto & p_bw = p_ej->getShells()[ilayer]->getBWs()[p_pars->ishell];
        /// ------------
        p_pars->ctheta0 = p_bw->getPars()->ctheta0;
        p_pars->ilayer = p_bw->getPars()->ilayer;
        p_pars->ishell = p_bw->getPars()->ishell;
        p_pars->ncells = (size_t) p_bw->getPars()->ncells;
        /// ----------
        // Vector & tburst, Vector & tt, Vector & r, Vector & theta,Vector & m_gam, Vector & m_bet,
        // Vector & freq_arr, Vector & synch_em, Vector & synch_abs,
        // size_t & i_end_r, size_t ish, size_t il, int loglevel, void * params
        i_end_r = m_data[PWN::Q::itburst].size();
        p_pars->p_syn = std::make_unique<MagnetarSynchrotron>(m_loglevel);
        p_eats = std::make_unique<EATS>(m_data[PWN::Q::itburst],
                                        m_data[PWN::Q::itt], m_data[PWN::Q::iR], m_data[PWN::Q::itheta],
                                        m_data[PWN::Q::iGamma],m_data[PWN::Q::ibeta],
                                        i_end_r, 0, layer(), m_loglevel, commonTables, p_pars);
//        p_eats->setFluxFunc(fluxDensPW);
        p_eats->fluxFuncPW = fluxDensPW;
    }

    std::unique_ptr<EATS> & getEATS(){ return p_eats; }
    std::unique_ptr<MagnetarSynchrotron> & getRad(){ return p_pars->p_syn; }
    std::unique_ptr<Ejecta> & getBoundaryBW(){ return p_ej; }
    Vector & getTbGrid() { return p_ej->getTbGrid(); }
    VecVector & getData(){ return m_data; }
    Vector & getData(PWN::Q q){ return m_data[q]; }

    size_t layer() const { return p_pars->ilayer; }
    size_t shell() const { return p_pars->ishell; }
    size_t ncells() const { return p_pars->ncells; }

    void updateOuterBoundary(Vector & r, Vector & beta, Vector & rho, Vector & tau, Vector & temp){
        if ((r[0] < 0) || (beta[0] < 0) || (rho[0] < 0) || (tau[0] < 0) || (temp[0] < 0)){
            (*p_log)(LOG_ERR,AT) << " wrong value\n";
            exit(1);
        }
        p_pars->rho_ej_curr = rho[0];
        p_pars->tau_ej_curr = tau[0];
        p_pars->r_ej_curr   = r[0];
        p_pars->v_ej_curr   = beta[0] * CGS::c;

//        p_pars->temp_ej_curr= temp[0];
        if (p_pars->v_ej_curr > CGS::c){
            (*p_log)(LOG_ERR,AT) << " p_pars->v_ej_curr > c betaSh[0]="<<beta[0]<<"\n";
            exit(1);
        }
//        std::cout << rho[0] << " "
//                  << tau[0] << " "
//                  << r[0] << " "
//                  << betaSh[0] * CGS::c
//                  << "\n";
//        int x = 1;
    }
    void updateOuterBoundary(const double r, const double beta, const double rho, const double tau, const double temp){
        if ((r < 0) || (beta < 0) || (rho < 0) || (tau < 0) || (temp < 0)){
            (*p_log)(LOG_ERR,AT) << " wrong value\n";
            exit(1);
        }
        p_pars->rho_ej_curr = rho;
        p_pars->tau_ej_curr = tau;
        p_pars->r_ej_curr   = r;
        p_pars->v_ej_curr   = beta * CGS::c;

//        p_pars->temp_ej_curr= temp[0];
        if (p_pars->v_ej_curr > CGS::c){
            (*p_log)(LOG_ERR,AT) << " p_pars->v_ej_curr > c betaSh="<<beta<<"\n";
            exit(1);
        }
//        std::cout << rho[0] << " "
//                  << tau[0] << " "
//                  << r[0] << " "
//                  << betaSh[0] * CGS::c
//                  << "\n";
//        int x = 1;
    }

    void updateMagnetar(double ldip, double lacc){
        if (ldip < 0 or !std::isfinite(ldip)){
            (*p_log)(LOG_ERR,AT) << " ldip < 0 or nan; ldip="<<ldip<<"\n";
        }
        if (lacc < 0 or !std::isfinite(lacc)){
            (*p_log)(LOG_ERR,AT) << " lacc < 0 or nan; lacc="<<lacc<<"\n";
        }
        // ----------------------
        p_pars->curr_ldip = ldip;
        p_pars->curr_lacc = lacc;
    }

    /// ---- PARAMETERS -------------------------------------------------------------
    void setPars(StrDbMap & pars, StrStrMap & opts, StrDbMap & main_pars, size_t iieq){
        double radius_wind_0 = getDoublePar("Rw0",pars,AT,p_log,-1,true); // PWN radius at t=0; [km]
//        radius_wind_0 *= (1000. * 100); // km -> cm

//        double vel_wind0 = radius_wind_0 / p_ej->getData(BW::Q::itburst)[0]; // initial wind velocity
        double eps_e = getDoublePar("eps_e",pars,AT,p_log,-1,true); // electron acceleration efficiency
        double eps_mag = getDoublePar("eps_mag",pars,AT,p_log,-1,true); // magnetic field efficiency
        double epsth0 = getDoublePar("eps_th",pars,AT,p_log,-1,true); // initial absorption fraction
        double albd_fac = getDoublePar("albd_fac",pars,AT,p_log,-1,true); // initial albedo fraction
        double gamma_b = getDoublePar("gamma_b",pars,AT,p_log,-1,true); //break Lorentz factor of electron injection spectrum
        int iterations = (int)getDoublePar("iters",pars,AT,p_log,-1,true); //iterations for absorption spectrum calcs
        run_pwn = getBoolOpt("run_pwn", opts, AT, p_log, false, true);
        load_pwn = getBoolOpt("load_pwn", opts, AT, p_log, false, true);
        p_pars->do_opt_depth_ej_calc = getBoolOpt("do_opt_depth_ej_calc", opts, AT, p_log, false, true);
        // **************************************
        p_pars->Rw0 = radius_wind_0;
//        p_pars->vel_w0 = vel_wind0;
        p_pars->eps_e = eps_e;
        p_pars->eps_mag = eps_mag;
        p_pars->eps_th = epsth0;
        p_pars->albd_fac = albd_fac;
        p_pars->gamma_b = gamma_b;
        p_pars->iieq = iieq;
        p_pars->iterations = iterations;
//        p_pars->nthreads_for_frac = nthreads_frac;
        // *************************************
        frac_psr_dep_.resize(p_pars->iterations + 1, 0.0);
        //-------------------------------------
        for (auto & key: {"n_ism", "d_l", "z", "theta_obs", "A0", "s", "r_ej", "r_ism"}) {
            if (main_pars.find(key) == main_pars.end()) {
                (*p_log)(LOG_ERR, AT) << " keyword '" << key << "' is not found in main parameters. \n";
                exit(1);
            }
            pars[key] = main_pars.at(key);
        }
        /// -----------------------------------
        p_pars->theta_obs = getDoublePar("theta_obs", pars, AT, p_log,-1, true);//pars.at("theta_obs");
        p_pars->d_l = getDoublePar("d_l", pars, AT, p_log,-1, true);//pars.at("d_l");
        p_pars->z = getDoublePar("z", pars, AT, p_log,-1, true);//pars.at("z");
        /// ----------------------------------
        auto & p_bw = p_ej->getShells()[p_pars->ilayer]->getBWs()[p_pars->ishell];

        p_eats->setEatsPars(
                pars, opts, p_bw->getPars()->nlayers, (size_t)p_bw->getPars()->ncells, p_bw->getPars()->ctheta0,
                p_bw->getPars()->theta_c_l, p_bw->getPars()->theta_c_h, p_bw->getPars()->theta_w, p_bw->getPars()->theta_max);
        i_end_r = m_data[PWN::Q::itburst].size(); // update for eats
//        int i = 1;
        p_pars->p_syn->setPars(pars,opts);
//        p_pars->p_syn->
    }
    /// -----------------------------------------------------------------------------

    PWNPars *& getPars(){ return p_pars; }

    void setInitConditions( double * arr, size_t i ) {
        double beta0 = p_pars->Rw0 / p_ej->getTbGrid()[0] / CGS::c; // m_tb_arr[0] * beta0 * CGS::c;
        double mom0 = MomFromBeta(beta0);
        arr[i + Q_SOL::i_Rw] = p_pars->Rw0;
//        arr[i + Q_SOL::i_mom] = EQS::MomFromBeta( v_w / CGS::c);;
        arr[i + Q_SOL::i_Enb] = p_pars->eps_e * p_pars->curr_ldip
                              + p_pars->eps_th * p_pars->curr_lacc;
        arr[i + Q_SOL::i_Epwn] = p_pars->eps_mag * p_pars->curr_ldip;
        arr[i + Q_SOL::i_tt] = EQS::init_elapsed_time(p_pars->Rw0,
                               mom0, false);
        p_pars->b_pwn = evalCurrBpwn(arr);
        p_pars->is_init = true;
    }

    void evaluateRhs( double * out_Y, size_t i, double x, double const * Y ){
        double r_w   = Y[i + Q_SOL::i_Rw];
        double e_nb  = Y[i + Q_SOL::i_Enb];
        double e_pwn = Y[i + Q_SOL::i_Epwn];
//        double mom   = Y[i + Q_SOL::i_mom];
        // ******************************************

        // ******************************************
        double rho_ej = p_pars->rho_ej_curr; // current ejecta density (shell?)
        double tau_ej = p_pars->tau_ej_curr; // current ejecta density (shell?)
        double r_ej = p_pars->r_ej_curr; // current ejecta density (shell?)
        double v_ej = p_pars->v_ej_curr; // current ejecta density (shell?)
        double ldip = p_pars->curr_ldip;
        double lacc = p_pars->curr_lacc;
        // ******************************************
        if (r_w > r_ej){
            r_w = r_ej;
        }
        // (see Eq. 28 in Kashiyama+16)
        double v_w = std::sqrt( (7./6.) * e_nb / (4. * CGS::pi * r_w*r_w*r_w * rho_ej) );
        if (v_w > CGS::c){
            (*p_log)(LOG_ERR,AT)<<"v_w/c="<<v_w/CGS::c<<"\n";
            exit(1);
        }
        // if (v_w > pow((2 * l_disk(t) * r_w / (3 - delta) / m_ej),1./3.)) # TODO finish this
        if (v_w > v_ej){
//            std::cerr << AT << "\t" << "v_w > v_ej\n";
            v_w = v_ej;
        }
//        v_w = v_ej; // TODO make a proper model...

        p_pars->mom=EQS::MomFromBeta(v_w/CGS::c);
        // computeSynchrotronEmissivityAbsorptionAnalytic nebula energy \int(Lem * min(1, tau_T^ej * V_ej / c))dt Eq.[28] in Eq. 28 in Kashiyama+16
        double dEnbdt = 0;
        if (tau_ej * (r_w / r_ej) > CGS::c / v_w){
            dEnbdt = (p_pars->eps_e * p_pars->curr_ldip
                   + p_pars->eps_th * p_pars->curr_lacc);
        }
        else{
            dEnbdt = (tau_ej * (r_w / r_ej) * (v_w/CGS::c)
                      * p_pars->eps_e * p_pars->curr_ldip
                      + p_pars->eps_th * p_pars->curr_lacc);
        }

        double tdyn = r_ej / v_ej;
        double dEpwndt = p_pars->eps_mag * p_pars->curr_ldip - e_pwn / tdyn;

        double dttdr = EQS::evalElapsedTime(r_w, p_pars->mom, 0., false);
        if (!std::isfinite(dttdr)||dttdr<=0){
            (*p_log)(LOG_ERR,AT)<<"dttdr="<<dttdr<<"\n";
            exit(1);
        }
        double drdt = v_w;// + r_w / x;
        /// Using pressure equilibrium, Pmag = Prad; Following approach (see Eq. 28 in Kashiyama+16)
        out_Y[i + Q_SOL::i_Rw] = drdt;
//        out_Y[i + Q_SOL::i_mom] = EQS::MomFromBeta( v_w / CGS::c);
        out_Y[i + Q_SOL::i_tt] = dttdr*drdt;
        out_Y[i + Q_SOL::i_Enb] = dEnbdt;
        out_Y[i + Q_SOL::i_Epwn] = dEpwndt;
    }


    void insertSolution( const double * sol, size_t it, size_t i ){
        m_data[PWN::Q::itburst][it] = p_ej->getTbGrid()[it];
        m_data[PWN::Q::iR][it]      = sol[i + Q_SOL::i_Rw];
        m_data[PWN::Q::iEnb][it]    = sol[i + Q_SOL::i_Enb];
        m_data[PWN::Q::iEpwn][it]   = sol[i + Q_SOL::i_Epwn];
        m_data[PWN::Q::itt][it]     = sol[i + Q_SOL::i_tt];
        m_data[PWN::Q::imom][it]    = p_pars->mom;//sol[i+Q_SOL::i_mom];
    }


    void addOtherVariables( size_t it ){
        double r_w = m_data[PWN::Q::iR][it];
        double u_b_pwn = 3.0*m_data[PWN::Q::iEpwn][it]/4.0/M_PI/r_w/r_w/r_w; // Eq.17 in Murase+15; Eq.34 in Kashiyama+16
        double b_pwn = pow(u_b_pwn*8.0*M_PI,0.5); //be careful: epsilon_B=epsilon_B^pre/8/PI for epsilon_B^pre used in Murase et al. 2018
        /// --------------------------------------------
        m_data[PWN::Q::iB][it]     = b_pwn;
        m_data[PWN::Q::iGamma][it] = EQS::GammaFromMom(m_data[PWN::Q::imom][it]);
        m_data[PWN::Q::ibeta][it]  = EQS::BetaFromMom(m_data[PWN::Q::imom][it]);
        m_data[PWN::Q::itheta][it] = p_pars->ctheta0;
        m_data[PWN::Q::iGammaTermShock][it] = EQS::GammaFromMom(m_data[PWN::Q::imom][it]); // TODO Replace with proper calc. of the temrination shock vel.
        m_data[PWN::Q::idr][it] = it > 0 ? m_data[PWN::Q::iR][it] - m_data[PWN::Q::iR][it-1] : 0.;// TODO replace with proper eval of 'dr' for emitting volume (around shock)
    }

    /// Get current PWN magnetic field
    double evalCurrBpwn( const double * Y ){
        if (run_pwn)
            return 0.;
        double r_w = Y[p_pars->iieq + Q_SOL::i_Rw];
        double u_b_pwn = 3.0*Y[p_pars->iieq + Q_SOL::i_Epwn]/4.0/M_PI/r_w/r_w/r_w; // Eq.17 in Murase+15; Eq.34 in Kashiyama+16
        double b_pwn = pow(u_b_pwn*8.0*M_PI,0.5); //be careful: epsilon_B=epsilon_B^pre/8/PI for epsilon_B^pre used in Murase et al. 2018
        p_pars->b_pwn = b_pwn;
        return b_pwn;
    }

    double getFacPWNdep( double rho_ej, double delta_ej, double T_ej, double Ye){
        if(!std::isfinite(rho_ej) || rho_ej < 0){
            (*p_log)(LOG_ERR,AT)<<"bad value in PWN frac calc: rho_ej="<<rho_ej<<"\n"; exit(1);
        }
        if(!std::isfinite(delta_ej) || delta_ej < 0){
            (*p_log)(LOG_ERR,AT)<<"bad value in PWN frac calc: delta_ej="<<delta_ej<<"\n"; exit(1);
        }
        if(!std::isfinite(T_ej) || T_ej < 0){
            (*p_log)(LOG_ERR,AT)<<"bad value in PWN frac calc: T_ej="<<T_ej<<"\n"; exit(1);
        }
        if(!std::isfinite(Ye) || Ye < 0){
            (*p_log)(LOG_ERR,AT)<<"bad value in PWN frac calc: Ye="<<Ye<<"\n"; exit(1);
        }

        int opacitymode=0; //0=iron, 1=Y_e~0.3-0.5, 2=Y_e~0.1-0.2, 3=CO
        if (Ye <= 0.2)
            opacitymode = 2; // r-process heavy
        else if ((Ye > 0.2) && (Ye <= 0.3))
            opacitymode = 1; // r-process light
        else if ((Ye > 0.3) && (Ye <= 0.5))
            opacitymode = 0;//0=iron-rich
        else if (Ye > 0.5)
            opacitymode = 3; // CO
        else{
            (*p_log)(LOG_ERR,AT) << " error \n";
            exit(1);
        }
        return facPSRdep(rho_ej, delta_ej, T_ej, opacitymode);
    }

    double getAbsobedMagnetarLum(double lsd, double lacc, double fac_pwn_dep){
        return fac_pwn_dep * (p_pars->eps_e * lsd + p_pars->eps_e * lacc);
    }

    bool isSolutionOk( double * sol, double x, size_t ix, size_t i ){
        bool is_ok = true;
        if (sol[i+Q_SOL::i_Enb] < 0){
            (*p_log)(LOG_ERR,AT)<<"Wrong Enb="<<sol[i+Q_SOL::i_Enb]<<"\n";
            is_ok = false;
        }
        return is_ok;
    }

    void applyUnits( double * sol, size_t i ){

    }

public:
    void evalElectronDistribution(){
        (*p_log)(LOG_ERR,AT)<<" not implemented\n";
        exit(1);
    }
    void computeSynchrotronSpectrum(){
        (*p_log)(LOG_ERR,AT)<<" not implemented\n";
        exit(1);
    }

private: // -------- RADIATION ----------

    double facPSRdep(const double rho_ej, const double delta_ej,
                     const double T_ej, const int opacitymode){
        if (p_pars->b_pwn < 0 || !std::isfinite(p_pars->b_pwn)){
            (*p_log)(LOG_ERR,AT)<<" b_pwn is not set\n";
            exit(1);
        }

        double e_gamma_min;
        double e_gamma_max_tmp;
        double e_gamma_gamma_ani_tmp;
        double e_gamma_syn_b_tmp;

        e_gamma_min = 1.0*EV_TO_ERG; // eV
        e_gamma_max_tmp = PWNradiationMurase::e_gamma_max(p_pars->b_pwn);
        e_gamma_gamma_ani_tmp = PWNradiationMurase::e_gamma_gamma_ani(T_ej);
        e_gamma_syn_b_tmp = PWNradiationMurase::e_gamma_syn_b(p_pars->b_pwn,p_pars->gamma_b);

        if (e_gamma_gamma_ani_tmp < e_gamma_max_tmp)
            e_gamma_max_tmp = e_gamma_gamma_ani_tmp;

        const int i_max = p_pars->iterations;
//        double e_tmp = e_gamma_min;
//        double del_ln_e = 0.0;

        const double albd_fac = p_pars->albd_fac;
//        int opacitymode = 0;

//        tmp(rho_ej,delta_ej,T_ej,p_pars->albd_fac,opacitymode,e_gamma_max_tmp,e_gamma_syn_b_tmp, e_gamma_min);
//        std::cout << " 11 \n";
        double frac_psr_dep_tmp = 0.0; int i = 0;
//        std::cout << " frac_psr_dep_.size(0" << frac_psr_dep_.size()<<"\n";
//        for (size_t i = 0; i <= p_pars->iterations; i++)
//            frac_psr_dep_tmp += frac_psr_dep_[i];
        int nthreads = 6;
        ///
        if (e_gamma_max_tmp > e_gamma_syn_b_tmp){
            double del_ln_e = (log(e_gamma_max_tmp)-log(e_gamma_min))/(double)(i_max+1);
#pragma omp parallel for num_threads( nthreads )
            for (i=0;i<=i_max;i++) {
                double e_tmp = e_gamma_min * exp(del_ln_e*(double)i);
                double f_gamma_dep_ = PWNradiationMurase::f_gamma_dep(e_tmp,rho_ej,delta_ej,albd_fac,opacitymode);
                double spec_non_thermal_ = PWNradiationMurase::spec_non_thermal(e_tmp,p_pars->b_pwn,p_pars->gamma_b,T_ej);
                double frac_psr_dep_tmp_ = f_gamma_dep_ * spec_non_thermal_ * e_tmp * del_ln_e;
                double frac_psr_dep_tmp_tmp = 0;
                if (i == 0 || i==i_max)
                    frac_psr_dep_tmp_tmp = (1.0/2.0) * frac_psr_dep_tmp_;
                else if (i % 2 == 0)
                    frac_psr_dep_tmp_tmp = (2.0/3.0) * frac_psr_dep_tmp_;
                else
                    frac_psr_dep_tmp_tmp = (4.0/3.0) * frac_psr_dep_tmp_;
                frac_psr_dep_[i] = frac_psr_dep_tmp_tmp;
            }
        }
        else {
            e_gamma_max_tmp = e_gamma_syn_b_tmp;
            double del_ln_e = (log(e_gamma_max_tmp)-log(e_gamma_min))/(double)(i_max+1);
#pragma omp parallel for num_threads( nthreads )
            for (i=0;i<=i_max;i++) {
                double e_tmp = e_gamma_min * exp(del_ln_e*(double)i);
                double f_gamma_dep_ = PWNradiationMurase::f_gamma_dep(e_tmp,rho_ej,delta_ej,albd_fac,opacitymode);
                double spec_non_thermal_ = PWNradiationMurase::spec_non_thermal(e_tmp,p_pars->b_pwn,p_pars->gamma_b,T_ej);
                double frac_psr_dep_tmp_ = f_gamma_dep_ * spec_non_thermal_ * e_tmp * del_ln_e;
                double frac_psr_dep_tmp_tmp = 0;
                if (i == 0 || i==i_max)
                    frac_psr_dep_tmp_tmp = (1.0/3.0) * frac_psr_dep_tmp_;
                else if (i % 2 == 0)
                    frac_psr_dep_tmp_tmp = (2.0/3.0) * frac_psr_dep_tmp_;
                else
                    frac_psr_dep_tmp_tmp = (4.0/3.0) * frac_psr_dep_tmp_;
                frac_psr_dep_[i]= frac_psr_dep_tmp_tmp;
            }
        }

        for (size_t i = 0; i <= i_max; ++i)
            frac_psr_dep_tmp += frac_psr_dep_[i];

        if (frac_psr_dep_tmp > 1.)
            frac_psr_dep_tmp = 1.;
        if (!std::isfinite(frac_psr_dep_tmp)||frac_psr_dep_tmp < 0){
            (*p_log)(LOG_ERR,AT) << "frac_psr_dep_tmp="<<frac_psr_dep_tmp<<"\n";
            exit(1);
        }
        return frac_psr_dep_tmp;
    }

public:

    enum METHOD_PWN_SPEC { inumBPL, ianaBPL };
    void evaluateComovingSpectrum(Vector & times, Vector & freqs, Vector & gams){

        if (times.size() < 1 || freqs.size() < 1 || gams.size() < 1){
            (*p_log)(LOG_ERR,AT)<<" error. "<< " times.size()="<<times.size()
                                << " freqs.size()="<<freqs.size()<< " gams.size()="<<gams.size()<<"\n";
            exit(1);
        }
        if (times[times.size()-1]>=m_data[PWN::Q::itburst][m_data[PWN::Q::itburst].size()-1]){
            (*p_log)(LOG_ERR,AT)<<" requested for spectrum times[times.size()-1]="<<times[times.size()-1]
            << " exceeds evolution timegrid end: "<< m_data[PWN::Q::itburst][m_data[PWN::Q::itburst].size()-1]<<"\n";
            exit(1);
        }
        if (times[0]<=m_data[PWN::Q::itburst][0]){
            (*p_log)(LOG_ERR,AT)<<" requested for spectrum times[0]="<<times[0]
                                << " below evolution timegrid start: "<< m_data[PWN::Q::itburst][0]<<"\n";
            exit(1);
        }
        if (p_pars->m_spectrum.size() < 1){
            p_pars->m_spectrum.resize(times.size()*freqs.size(),0.);
            p_pars->m_freq_arr.resize(times.size()*freqs.size(),0.);
            p_pars->m_freq_arr = freqs;
            p_pars->m_time_arr = times;
            p_pars->m_r_arr.resize(times.size());
//            for (size_t ii = 0; ii < times.size()*freqs.size(); ii++)
//                p_pars->m_freq_arr[ii] = freqs[ii];
        }
        if (p_pars->m_synch_em.size() < 1){
            p_pars->m_synch_em.resize(times.size()*freqs.size(),0.);
        }
        if (p_pars->m_synch_abs.size() < 1){
            p_pars->m_synch_abs.resize(times.size()*freqs.size(),0.);
        }

#if 1
        /// assuming that the whole radial grid is to be used for calculations
        if ((times[0]==m_data[PWN::Q::itburst][0])&&
            (times[times.size()-1]==m_data[PWN::Q::itburst][m_data[PWN::Q::itburst].size()-1])&&
                (times.size()==m_data[PWN::Q::itburst].size())){
            Vector drnb(m_data[PWN::Q::iR].size());
            Vector vnb(m_data[PWN::Q::iR].size());
            Vector lpsr(m_data[PWN::Q::iR].size());
            for (size_t it = 1; it < m_data[PWN::Q::iR].size(); it++){
                double dr = m_data[PWN::Q::iR][it] - m_data[PWN::Q::iR][it-1]; // TODO this is resolution dependent...
                double rnb = m_data[PWN::Q::iR][it];
                rnb = m_data[PWN::Q::iR][it];
                drnb[it] = dr;
                vnb[it] = 4.*M_PI*rnb*rnb*dr / (double)ncells(); // TODO is it a proper place to add ncells()
                double t = m_data[PWN::Q::itburst][it];
                lpsr[it] = p_mag->getMagValInt(MAG::Q::ildip, t) / (double)ncells(); // TODO is it a proper place to add ncells()
                p_pars->m_r_arr[it] = rnb;
            }
            p_pars->p_syn->computeComovingSpectrum(
                    p_pars->m_spectrum, p_pars->m_synch_em, p_pars->m_synch_abs,
                    m_data[PWN::Q::itburst],freqs,gams, m_data[PWN::Q::iB], vnb, drnb, lpsr );
        }
        /// assuming that only a fraction of the grid is to be used (interpolate between)
        else{
            Vector drnb(times.size());
            Vector vnb(times.size());
            Vector lpsr(times.size());
            Vector Bnb(times.size());
            for (size_t it = 1; it < times.size(); it++){
                double t = times[it];
                size_t ia = findIndex(t, m_data[PWN::Q::itburst], m_data[PWN::Q::itburst].size());
                size_t ib = ia + 1;
                Bnb[it] = interpSegLog(ia, ib, times[it], m_data[PWN::Q::itburst], m_data[PWN::Q::iB]);
                double rnb_m1 = interpSegLog(ia, ib, times[it-1], m_data[PWN::Q::itburst], m_data[PWN::Q::iR]);
                double rnb = interpSegLog(ia, ib, times[it], m_data[PWN::Q::itburst], m_data[PWN::Q::iR]);
                double dr = rnb - rnb_m1;
                drnb[it] = dr;
                vnb[it] = 4.*M_PI*rnb*rnb*dr / (double)ncells(); // TODO is it a proper place to add ncells()
                lpsr[it] = p_mag->getMagValInt(MAG::Q::ildip, times[it]) / (double)ncells(); // TODO is it a proper place to add ncells()
                p_pars->m_r_arr[it] = rnb;
            }
            p_pars->p_syn->computeComovingSpectrum(
                    p_pars->m_spectrum, p_pars->m_synch_em, p_pars->m_synch_abs,
                    times, freqs, gams, Bnb, vnb, drnb, lpsr );
        }
#endif
#if 0
//        std::cout << "times = "<<times.size()<<"\n";
//        std::cout << "freqs = "<<freqs.size()<<"\n";

//        Vector _times(freqs.size() * times.size(), 0.0);
//        Vector _freqs(freqs.size() * times.size(), 0.0);
//        size_t ii = 0;
//        for (double freq: freqs) {
//            for (double time: times) {
//                _times[ii] = time;
//                _freqs[ii] = freq;
//                ii++;
//            }
//        }


        METHOD_PWN_SPEC methodPwnSpec = inumBPL;

        double gam_b = 1.e3;
        double p1 = -5.;
        double p2 = 3.;
        double eps_e = 0.99;


        if (methodPwnSpec == inumBPL){
            double gam_max = 1.e6;
            int Nbin_e = 512;
            int Nbin_ph = (int)freqs.size();

//            double gam_b = 1.e3;

            double gam[Nbin_e],dN_dgam[Nbin_e],dgam[Nbin_e],dN_dgam_dt[Nbin_e],dgam_dt[Nbin_e],tad[Nbin_e],tsyn[Nbin_e];
            double N_inj_tot=0.;
            initialize_e_dis(gam,dgam,dN_dgam,dN_dgam_dt,dgam_dt,tad,tsyn,gam_max,Nbin_e);

            double gam_ph_min = 1.e-14;
            double gam_ph_max = 1.e4;
            double gam_ph[Nbin_ph],P_nu_syn[Nbin_ph],alpha_nu_syn[Nbin_ph];
            initialize_ph_dis(gam_ph,P_nu_syn,alpha_nu_syn,gam_ph_min,gam_ph_max,Nbin_ph);
            for (size_t inu = 0; inu < freqs.size(); inu++)
                gam_ph[inu] = freqs[inu] / (CGS::MeC2 / CGS::H); // Hz -> erg
//        std::cout << gam_ph[0]*MeC2/H/1.0e9 << " = "<< freqs[0]/1e9<<"\n";


//#pragma omp parallel for private(Bnb,rnb,dr,Lpsr,dt,\
//        gam,dN_dgam,dgam,dN_dgam_dt,dgam_dt,tad,tsyn \
//        gam_ph,P_nu_syn,alpha_nu_syn \
//        gam_max,Nbin_e,Nbin_ph,eps_e,gam_b,p1,p2,N_inj_tot \
//        ) shared(times,freqs) num_threads( 6 )
            size_t ii = 0;
            for (size_t it = 1; it < times.size(); it++){
                double Bnb = 0., rnb=0.,rnb_m1=0.,dr=0.,Lpsr=0.,dt=0.;
                double t = times[it];
                dt = times[it] - times[it - 1];

                if(true) {
                    size_t ia = findIndex(t, m_data[PWN::Q::itburst], m_data[PWN::Q::itburst].size());
                    size_t ib = ia + 1;
                    Bnb = interpSegLog(ia, ib, t, m_data[PWN::Q::itburst], m_data[PWN::Q::iB]);
                    rnb = interpSegLog(ia, ib, t, m_data[PWN::Q::itburst], m_data[PWN::Q::iR]);
                    rnb_m1 = interpSegLog(ia, ib, times[it-1], m_data[PWN::Q::itburst], m_data[PWN::Q::iR]);
                }
                else{
                    Bnb = m_data[PWN::Q::iB][it];
                    rnb = m_data[PWN::Q::iR][it];
                    rnb_m1 = m_data[PWN::Q::iR][it-1];
                }

                dr = rnb-rnb_m1;
                double vnb = 4.*M_PI*rnb*rnb*dr / (double)ncells();
                Lpsr = p_mag->getMagValInt(MAG::Q::ildip, t) / (double)ncells() ; // TODO correct for cells earlier!
                /// comptue synchrotron spectrum
                if (it > 1)
                    calc_syn_spec(Bnb,vnb,dr,gam,dgam,dN_dgam,gam_max,Nbin_e,gam_ph,P_nu_syn,alpha_nu_syn,Nbin_ph);
                double number_conservation = Ntot(dgam,dN_dgam,Nbin_e)/N_inj_tot;
                if (number_conservation > 1.2 or number_conservation < 0.9){
                    std::cerr << AT << " conserv="<<number_conservation<<"\n";
                }
                cooling(t,Bnb,dgam_dt,gam,tad,tsyn,Nbin_e);
                injection(gam, dgam, dN_dgam_dt, dgam_dt, Lpsr, dt, &N_inj_tot, eps_e, gam_b, gam_max, p1, p2, Nbin_e);
                time_evolution_e(dt,gam,dgam,dN_dgam,dN_dgam_dt,dgam_dt,Nbin_e);
                /// --------------------------
                if ((!std::isfinite(P_nu_syn[0]))||(!std::isfinite(P_nu_syn[Nbin_ph-1])) || (P_nu_syn[0] < 0)||(P_nu_syn[Nbin_ph-1] < 0)){
                    (*p_log)(LOG_ERR,AT) << " nan in pwn spec P_nu_syn[0]=" << P_nu_syn[0] << "\n";
                    std::cout << "Bnb="<<Bnb<<" rnb="<<rnb<<" gam[0]="<<gam[0]<<" dgam[0]="<<dgam[0]<<" dN_dgam[0]="<<dN_dgam[0]<<" gam_max="<<gam_max
                              <<" Nbin_e="<<Nbin_e<<" Lpsr="<<Lpsr<<" n_ing_tot="<<N_inj_tot<<" number_conserv="<<number_conservation<<"\n";
                    exit(1);
                }
//                spec[ii] = gam_ph[inu] * CGS::MeC2 / CGS::H * P_nu_syn[inu]; // [erg/s/Hz]
                for (size_t inu = 0; inu < freqs.size();  inu++) {
                    p_pars->out_spectrum[ii] = P_nu_syn[inu]; // TODO ? IS IT TRUE?
                    p_pars->out_specturm_ssa[ii] = alpha_nu_syn[inu];
                    p_pars->m_spectrum[ii] = gam_ph[inu] * CGS::MeC2 / CGS::H * P_nu_syn[inu]; // [erg/s/Hz]
                    ii++;
                }


//                spec[it] = gam_ph[it] * CGS::MeC2 / CGS::H * P_nu_syn[it]; // [erg/s/Hz]
            }
        }
        else if (methodPwnSpec == ianaBPL){
            /// From 2.2 Sec. Emission from the PWN in MNRAS 512, 5572–5579 (2022) https://doi.org/10.1093/mnras/stac797
//            double p1 = 1.5;
//            double p2 = 2.5;

//    double eps_e = 0.1;
            double gamma_m = 3.;
            size_t ii = 0;
            for (size_t it = 1; it < times.size(); it++) {
                double Bnb = 0., rnb=0.,dr=0.,Lpsr=0.,dt=0.;
                double tcomov = times[it]; // TODO THIS IS NOT COMOVING TIME !!!
                dt = times[it] - times[it - 1];

                if (true) {
                    size_t ia = findIndex(times[it], m_data[PWN::Q::itburst], m_data[PWN::Q::itburst].size());
                    size_t ib = ia + 1;
                    Bnb = interpSegLog(ia, ib, times[it], m_data[PWN::Q::itburst], m_data[PWN::Q::iB]);
                    rnb = interpSegLog(ia, ib, times[it], m_data[PWN::Q::itburst], m_data[PWN::Q::iR]);
                } else {
                    Bnb = m_data[PWN::Q::iB][it];
                    rnb = m_data[PWN::Q::iR][it];
                }
                Lpsr = p_mag->getMagValInt(MAG::Q::ildip, times[it]) / (double)ncells() ; // TODO correct for cells earlier!

//    double gamma_b = 1e4; // ????
                double gamma_c = 6. * M_PI * CGS::M_ELEC * CGS::c / (SIGMA_T * Bnb * Bnb * tcomov);
                double gamma_M = std::sqrt(9. * CGS::M_ELEC * CGS::M_ELEC * CGS::c * CGS::c * CGS::c * CGS::c /
                                           (8. * Bnb * CGS::ELEC * CGS::ELEC * CGS::ELEC));
                ///
                double nu_m = nu_from_gamma(Bnb, gamma_m);
                double nu_c = nu_from_gamma(Bnb, gamma_c);
                double nu_M = nu_from_gamma(Bnb, gamma_M);
                double nu_b = nu_from_gamma(Bnb, gam_b);
                double nu_a = 0.;

                double xi_q1 = 5. / 3.;
                double n_pwn = Lpsr / (4. * M_PI * rnb * rnb * gam_b * CGS::M_ELEC * CGS::c * CGS::c * CGS::c);

                double Rb = 1. / (2. - p1) + 1. / (p2 - 2.);
                double eta = std::min(1., std::pow(nu_b / nu_c, (p2 - 2.) / 2.));
                double xi = eta * eps_e;
                double prefac = xi * Lpsr / (2. * Rb);

                for (size_t ifreq = 0; ifreq < freqs.size(); ifreq++) {
                    double nu = freqs[ifreq];

//    std::vector<double> nu_arr = MakeLogspaceVec(8.,22.,100.,10);
//    std::vector<double> spec (nu_arr.size());
                    /// find nu_a from optical depth = 1
                    double tau_a = 1.;
//        xi_q1 * ELEC * n_ext * r_pwn / b_pwn / std::pow(gamma_m,5.) * std::pow(nu / nu_m, -p1/2. - 2.);
                    nu_a = nu_m * std::pow(tau_a * xi_q1 * CGS::ELEC * (n_pwn) * rnb / Bnb / std::pow(gamma_m, 5.)
                                           * std::pow(nu / nu_m, -p1 / 2. - 2.), p1 / 2. + 2.);

                    double spec = 0.;
                    if (nu_c < nu_b) {
                        /// fast coocling
                        if ((nu_m <= nu) && (nu <= nu_a))
                            spec = std::pow(nu_c / nu_b, (2. - p1) / 2.)
                                   * std::pow(nu_a / nu_c, (3. - p1) / 2.)
                                   * std::pow(nu / nu_a, 5. / 2.);
                        else if ((nu_a <= nu) && (nu <= nu_c))
                            spec = std::pow(nu_c / nu_b, (2. - p1) / 2.)
                                   * std::pow(nu / nu_c, (3. - p1) / 2.);
                        else if ((nu_c <= nu) && (nu <= nu_b))
                            spec = std::pow(nu / nu_b, (2. - p1) / 2.);
                        else if ((nu_b <= nu) && (nu <= nu_M))
                            spec = std::pow(nu / nu_b, (2. - p2) / 2.);
                        else {
                            std::cerr << " error in spectrum \n";
                            exit(1);
                        }
                    } else if (nu_c >= nu_b) {
                        if ((nu_m <= nu) && (nu <= nu_a))
                            spec = std::pow(nu_b / nu_c, (3. - p2) / 2.)
                                   * std::pow(nu_a / nu_b, (3. - p1) / 2.)
                                   * std::pow(nu / nu_a, 5. / 2.);
                        else if ((nu_a <= nu) && (nu <= nu_b))
                            spec = std::pow(nu_b / nu_c, (3. - p2) / 2.)
                                   * std::pow(nu / nu_b, (3. - p1) / 2.);
                        else if ((nu_b <= nu) && (nu <= nu_c))
                            spec = std::pow(nu / nu_c, (3. - p2) / 2.);
                        else if ((nu_c <= nu) && (nu <= nu_M))
                            spec = std::pow(nu / nu_c, (2. - p2) / 2.);
                        else {
                            std::cerr << " error in spectrum \n";
                            exit(1);
                        }
                    }
                    double fnu = prefac * spec;
//                    spec[it] = fnu;
                    p_pars->m_spectrum[ii] = fnu;
                    ii ++ ;
                }
            }
        }
#endif
    }
#if 0
    static void fluxDensPW_old(double & flux_dens, double & tau_comp, double & tau_BH, double & tau_bf,
                           double r, double & ctheta,
                           double phi, double theta, size_t ia, size_t ib, double ta, double tb,
                           double mu, double t_obs, double nu_obs, void * params){

        auto * p_pars = (struct PWNPars *) params;
        auto & p_ej = p_pars->p_ej;
        auto & p_bw = p_ej->getShells()[p_pars->ilayer]->getBWs()[p_pars->ishell];
        auto & p_mag = p_pars->p_mag;
        auto & m_data = p_pars->m_data;
//        if (p_pars->i_end_r==0)
//            return;
        ctheta = p_pars->ctheta0;//interpSegLin(ia, ib, t_obs, ttobs, m_data[BW::Q::ictheta]);
        double Gamma = interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::iGamma]);
        double b_pwn = interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::iB]);
        double temp  = interpSegLog(ia, ib, ta, tb, t_obs, p_bw->getData(BW::Q::iEJtemp));
        double tburst= interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::itburst]);
        double l_dip = p_mag->getMagValInt(MAG::Q::ildip, tburst) / (double)p_pars->ncells; // TODO this has to be done before Radiation Calcs.
        double l_acc = p_mag->getMagValInt(MAG::Q::ilacc, tburst) / (double)p_pars->ncells;

//        std::cout << "tburst="<<tburst<<" ldip="<<l_dip<<" lacc="<<l_acc<<"\n";

        double betaSh = EQS::Beta(Gamma);
        double a = 1.0 - betaSh * mu; // beaming factor
        double delta_D = Gamma * a; // doppler factor
        double nuprime = ( 1.0 + p_pars->z ) * nu_obs * delta_D;
        double nu_erg = nu_obs*4.1356655385381E-15*CGS::EV_TO_ERG;
        double gamma_b = 1.0e5; /* break Lorentz factor of electron injection spectrum */
//        double spec = PWNradiationMurase::spec_non_thermal(nu_erg, b_pwn, gamma_b, temp);

        double f_gamma_esc_x=1.;
        if (true)
            p_ej->evalOptDepthsAlongLineOfSight(f_gamma_esc_x, tau_comp, tau_BH, tau_bf,
                                                ctheta, r, CGS::pi/4., theta,
                                                CGS::pi/4., p_pars->theta_obs, p_pars->d_l,
                                                tburst, nuprime, p_pars->ilayer, params);

        double lum = p_pars->eps_e*(l_dip+l_acc)*PWNradiationMurase::spec_non_thermal(nu_erg, b_pwn, gamma_b, temp)*f_gamma_esc_x*nu_erg;
        flux_dens = lum * (1.0 + p_pars->z) / (2.0 * p_pars->d_l * p_pars->d_l);
//        double fnu = lum / p_pars->d_l / p_pars->d_l;
//        double npwn = l_dip / (4.*CGS::pi*r*r*gamma_b*CGS::me*CGS::c*CGS::c*CGS::c);
//        double nej = 0.;
//        double next = npwn + nej;
//        double fnu_ = pwn_spectrum(nuprime,r,b_pwn,next,tburst,(l_dip+l_acc),p_pars->eps_e,gamma_b);
//        double fnu = fnu_ * (1.0 + p_pars->z) / (2.0 * p_pars->d_l * p_pars->d_l);
//        flux_dens = fnu;
//        int x = 1;
    }
#endif
// flux_dens, tau_comp, tau_BH, tau_bf, r[i], ctheta, ctheta_cell, phi_cell,
//  ia[i], ib[i], ta[i], tb[i], mu[i], t_obs, nu_obs, _params
    static void fluxDensPW(double & flux_dens, double & tau_comp, double & tau_BH, double & tau_bf,
                           double r, double & ctheta, double theta, double phi,
                           size_t ia, size_t ib, double ta, double tb, double mu,
                           double t_obs, double nu_obs, void * params){

        auto * p_pars = (struct PWNPars *) params;
        auto & p_ej = p_pars->p_ej;
        auto & p_bw = p_ej->getShells()[p_pars->ilayer]->getBWs()[p_pars->ishell];
        auto & p_mag = p_pars->p_mag;
        auto & m_data = p_pars->m_data;

        if ((p_pars->m_freq_arr.size() < 1) || (p_pars->m_synch_em.size()< 1) ){
            (*p_pars->p_log)(LOG_ERR,AT)<<" m_freq_arr.size()="<<p_pars->m_freq_arr.size() <<" and "
            <<" out_spectrum.size()="<<p_pars->m_synch_em.size()
            <<"\n";
            exit(1);
        }
#if 1
        Interp2d int_em(p_pars->m_freq_arr, p_pars->m_r_arr, p_pars->m_synch_em); // m_data[PWN::Q::iR]
        Interp2d int_abs(p_pars->m_freq_arr, p_pars->m_r_arr, p_pars->m_synch_abs);
        Interp1d::METHODS mth = Interp1d::iLagrangeLinear;

//        ctheta = interpSegLin(ia, ib, ta, tb, t_obs, m_data[PWN::Q::ictheta]);
        double Gamma = interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::iGamma]);
        double beta  = interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::ibeta]);
        double tburst= interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::itburst]);
        ctheta = p_pars->ctheta0;
        // double GammaSh = ( Interp1d(m_data[BW::Q::iR], m_data[BW::Q::iGammaFsh] ) ).Interpolate(r, mth );
        /// compute Doppler factor
        double a = 1.0 - beta * mu; // beaming factor
        double delta_D = Gamma * a; // doppler factor
        /// compute the comoving obs. frequency from given one in obs. frame
        double nuprime = (1.0 + p_pars->z) * nu_obs * delta_D;
        if (nuprime < p_pars->m_freq_arr[0]) {
            (*p_pars->p_log)(LOG_WARN, AT) << " freqprime=" << nuprime << " < freq_arr[0]="
                << p_pars->m_freq_arr[0] << "\n";
            return;
        }
        if (nuprime > p_pars->m_freq_arr[p_pars->m_freq_arr.size()-1]) {
            (*p_pars->p_log)(LOG_WARN, AT) << " freqprime=" << nuprime << " > freq_arr[-1]="
                << p_pars->m_freq_arr[p_pars->m_freq_arr.size() - 1] << "\n";
            return;
        }



        size_t ia_nu = findClosestIndex(nuprime,p_pars->m_freq_arr,p_pars->tmp_ia_nu);// findIndex(nuprime, p_pars->m_freq_arr, p_pars->m_freq_arr.size());
        size_t ib_nu = ia_nu + 1;
        size_t ia_r = findClosestIndex(r,p_pars->m_r_arr,p_pars->tmp_ia_r);//findIndex(r, p_pars->m_r_arr, p_pars->m_r_arr.size());
        if (ia_r >= p_pars->m_r_arr.size() - 1) {
            return;
        }
        size_t ib_r = ia_r + 1;

        p_pars->tmp_ia_nu = ia_nu;
        p_pars->tmp_ia_r = ia_r;

        /// interpolate the emissivity and absorption coefficines
//                double em_prime = int_em.Interpolate(nuprime, r, mth);
        double em_prime = int_em.InterpolateBilinear(nuprime, r, ia_nu, ib_nu, ia_r, ib_r);
        if (!std::isfinite(em_prime)) {
            (*p_pars->p_log)(LOG_ERR,AT) << " em_prime="<<em_prime<<"\n";
            em_prime = 0.;
        }
//                double abs_prime = int_abs.Interpolate(nuprime, r, mth);
        double abs_prime = int_abs.InterpolateBilinear(nuprime, r, ia_nu, ib_nu, ia_r, ib_r);
        if (!std::isfinite(abs_prime)) {
            (*p_pars->p_log)(LOG_ERR,AT) << " abs_prime="<<abs_prime<<"\n";
            abs_prime = 0.;
        }
//        flux_dens=em_prime;
//        return;

        /// convert to the laboratory frame
        double em_lab = em_prime / (delta_D * delta_D); // conversion of emissivity (see vanEerten+2010)
        double abs_lab = abs_prime * delta_D; // conversion of absorption (see vanEerten+2010)

        /// computeSynchrotronEmissivityAbsorptionAnalytic optical depth (for this shock radius and thickness are needed)
        double GammaShock = interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::iGammaTermShock]);
        double dr = interpSegLog(ia, ib, ta, tb, t_obs, m_data[PWN::Q::idr]);
        double dr_tau = dr;////EQS::shock_delta(r, GammaShock); // TODO this is added becasue in Johanneson Eq. I use ncells

        double beta_shock = EQS::BetaFromGamma(GammaShock);
        double ashock = (1.0 - mu * beta_shock); // shock velocity beaming factor
        dr /= ashock; // TODO why is this here? What it means? Well.. Without it GRB LCs do not work!
        dr_tau /= ashock;
        double dtau;
        if(mu == beta_shock)
            dtau = 1.0e100; // HUGE VAL, just in case
        else
            dtau = abs_lab * dr * (1. - mu * beta_shock) / (mu - beta_shock);
        double intensity = computeIntensity(em_lab, dtau, METHOD_TAU::iSMOOTH);
        flux_dens = (intensity/* * r * r * dr*/) * (1.0 + p_pars->z) / (2.0 * p_pars->d_l * p_pars->d_l);

        double f_gamma_esc_x=1.;
        if (false)
            p_ej->evalOptDepthsAlongLineOfSight(f_gamma_esc_x, tau_comp, tau_BH, tau_bf,
                                                ctheta, r, phi, theta,
                                                CGS::pi/4.,p_pars->theta_obs, p_pars->d_l, p_pars->ilayer,
                                                t_obs, nuprime, p_pars->z, p_pars->ilayer, params);
        flux_dens *= f_gamma_esc_x;
        if (!std::isfinite(flux_dens)){
            (*p_pars->p_log)(LOG_ERR,AT)<<" flux_dens="<<flux_dens<<"\n";
            exit(1);
        }
#endif
    }
};



/// Container for independent layers of PWN model
class PWNset{
    CommonTables & commonTables;
    std::unique_ptr<Magnetar> & p_mag;
    std::unique_ptr<Ejecta> & p_ej;
    std::unique_ptr<Output> p_out = nullptr;
    std::vector<std::unique_ptr<PWNmodel>> p_pwns{};
    std::unique_ptr<logger> p_log;
    int loglevel;
    bool is_obs_pars_set = false;
//    bool is_synch_computed = false;
    Vector lc_freqs{};
    Vector lc_times{};
    Vector spec_times{};
    Vector spec_gams{};
    Vector spec_freqs{};
    Vector skymap_freqs{};
    Vector skymap_times{};
public:
    PWNset(std::unique_ptr<Magnetar> & p_mag, std::unique_ptr<Ejecta> & p_ej, CommonTables & commonTables, int loglevel)
        : p_mag(p_mag), p_ej(p_ej), commonTables(commonTables), loglevel(loglevel){
        p_log = std::make_unique<logger>(std::cout, std::cerr, loglevel, "PWNset");
        p_out = std::make_unique<Output>(loglevel);
//        m_nlayers = p_ej->nlayers();
//        m_nshells = p_ej->nshells();
//        m_ncells = p_ej->ncells();
    }
    StrDbMap pwn_pars{}; StrStrMap pwn_opts{};
    std::string workingdir{};
    bool run_pwn = false, save_pwn = false, load_pwn = false, do_ele=false, do_lc=false, do_skymap=false, do_spec=false;
    bool update_dens_struct = false;
    std::vector<std::unique_ptr<PWNmodel>> & getPWNs(){return p_pwns;}
    std::unique_ptr<PWNmodel> & getPWN(size_t i){return p_pwns[i];}
    size_t getNeq(){
        if (!run_pwn){
            return 0;
        }
        if (p_pwns.empty()){
            (*p_log)(LOG_ERR,AT) << "PWN is not initialized\n";
            exit(1);
        }
        size_t neq = nlayers() * p_pwns[0]->getNeq();
        return neq;
    };
    size_t nlayers() const {return (run_pwn||load_pwn) ? p_ej->nlayers() : 0;}
    size_t nshells() const {return 1;}//{return (run_pwn||load_pwn) ? p_ej->nshells() : 0;}
    int ncells() const {
//        return run_bws ? (int)ejectaStructs.structs[0].ncells : 0;
        return (run_pwn || load_pwn) ? (int)p_ej->ncells() : 0;
    }
    void setPars(StrDbMap & pars, StrStrMap & opts, std::string & working_dir, std::string parfilename,
                 Vector & tarr, StrDbMap & main_pars, size_t ii_eq){
        /// read pwn parameters of the pwn
        pwn_pars = pars;
        pwn_opts = opts;
        workingdir = working_dir;
//        m_nlayers = p_ej->nlayers(); // as ejecta is  set later
//        m_nshells = p_ej->nshells();
        if ((!pwn_pars.empty()) || (!pwn_pars.empty())) {
            run_pwn = getBoolOpt("run_pwn", pwn_opts, AT, p_log, false, true);
            save_pwn = getBoolOpt("save_pwn", pwn_opts, AT, p_log, false, true);
            load_pwn = getBoolOpt("load_pwn", pwn_opts, AT, p_log, false, true);
//            do_ele = getBoolOpt("do_ele", m_opts, AT, p_log, false, true);
            do_spec = getBoolOpt("do_spec", pwn_opts, AT, p_log, false, true);
            do_lc = getBoolOpt("do_lc", pwn_opts, AT, p_log, false, true);
            do_skymap = getBoolOpt("do_skymap", pwn_opts, AT, p_log, false, true);
        }
        else {
            (*p_log)(LOG_INFO, AT) << "PWN is not initialized and will not be considered.\n";
        }
        if (!(run_pwn || load_pwn))
            return;
        for(size_t il = 0; il < nlayers(); il++) {
//            auto & _x = p_ej->getShells()[il]->getBottomBW();
            p_pwns.push_back( std::make_unique<PWNmodel>( p_mag,
                                                          p_ej, il,//->getShells()[il]->getBW(0),
                                                          commonTables,
                                                          run_pwn) );
            p_pwns[il]->setPars(pars, opts, main_pars, ii_eq);
            ii_eq += p_pwns[il]->getNeq();
        }
        // --------------------------------------------
        for (auto &key: {"n_ism", "d_l", "z", "theta_obs", "A0", "s", "r_ej", "r_ism"}) {
            if (main_pars.find(key) == main_pars.end()) {
                (*p_log)(LOG_ERR, AT) << " keyword '" << key << "' is not found in main parameters. \n";
                exit(1);
            }
            pwn_pars[key] = main_pars.at(key);
        }

    }
    void setInitConditions(double * m_InitData){
//        if (!run_pwn)
//            return;
        for (size_t il=0; il < nlayers(); il++) {
            auto &ej_pwn = getPWNs()[il];
            ej_pwn->setInitConditions(m_InitData, ej_pwn->getPars()->iieq);
        }
    }

    /// output
    void savePWNdyn(StrDbMap & main_pars){
        (*p_log)(LOG_INFO,AT) << "Saving PWN BW dynamics...\n";
        auto fname = getStrOpt("fname_dyn", pwn_opts, AT, p_log, "", true);
        size_t every_it = (int)getDoublePar("save_pwn_every_it", pwn_pars, AT, p_log, 1, true);

        if (every_it < 1){
            std::cerr << " every_it must be > 1; Given every_it="<<every_it<<" \n Exiting...\n";
            std::cerr << AT << "\n";
            exit(1);
        }
        auto & models = getPWNs();
        auto & t_arr = models[0]->getTbGrid();
        std::vector<std::vector<double>> tot_dyn_out ( nshells() * nlayers() * PWN::m_vnames.size() );
        for (auto & arr : tot_dyn_out)
            arr.resize(t_arr.size());
        std::vector<std::string> arr_names{};
        size_t ii = 0;
        for (size_t ishell = 0; ishell < nshells(); ishell++){
            for(size_t ilayer = 0; ilayer < nlayers(); ilayer++){
                for (size_t ivar = 0; ivar < PWN::m_vnames.size(); ivar++) {
                    arr_names.push_back("shell="+std::to_string(ishell)
                                        +" layer="+std::to_string(ilayer)
                                        +" key="+PWN::m_vnames[ivar]);
                    auto & pwn = models[ilayer];//->getBW(ishell);
                    for (size_t it = 0; it < t_arr.size(); it++)
                        tot_dyn_out[ii][it] = pwn->getData(static_cast<PWN::Q>(ivar))[it];
                    size_t size = tot_dyn_out[ii].size();
                    auto & x = tot_dyn_out[ii];
                    ii++;
                }
            }
        }

        std::unordered_map<std::string, double> attrs{
                {"nshells", nshells() },
                {"nlayers", nlayers() },
                {"ntimes", t_arr.size() },
//                {"ncells", ncells() }
        };
        for (auto& [key, value]: main_pars) { attrs[key] = value; }
        for (auto& [key, value]: pwn_pars) { attrs[key] = value; }
        p_out->VectorOfVectorsH5(tot_dyn_out,arr_names,workingdir+fname,attrs);
    }

    /// input
    void loadPWNDynamics(){
        auto fname = getStrOpt("fname_dyn", pwn_opts, AT, p_log, "", true);
        if (!std::experimental::filesystem::exists(workingdir+fname))
            throw std::runtime_error("File not found. " + workingdir+fname);
        Exception::dontPrint();
        H5std_string FILE_NAME(workingdir+fname);
        H5File file(FILE_NAME, H5F_ACC_RDONLY);
        size_t nshells_ = (size_t)getDoubleAttr(file,"nshells");
        size_t nlayers_ = (size_t)getDoubleAttr(file, "nlayers");
        size_t ntimes_ = (size_t)getDoubleAttr(file, "ntimes");
        if (nshells_ != nshells()){
            (*p_log)(LOG_ERR,AT) << "Wring attribute: nshells_="<<nshells_<<" expected nshells="<<nshells()<<"\n";
//            exit(1);
        }
        if (nlayers_ != nlayers()){
            (*p_log)(LOG_ERR,AT) << "Wring attribute: nlayers_="<<nlayers_<<" expected nlayers_="<<nlayers()<<"\n";
//            exit(1);
        }
//        if (ntimes_ != ()){
//            (*p_log)(LOG_ERR,AT) << "Wring attribute: nlayers_="<<nlayers_<<" expected nlayers_="<<m_nlayers()<<"\n";
//            exit(1);
//        }
        //        double ntimes = getDoubleAttr(file, "ntimes");
        auto & models = getPWNs();
        for (size_t ish = 0; ish < nshells(); ish++) {
            for (size_t il = 0; il < nlayers(); il++) {
                auto & pwn = models[il];//->getBW(ish);
                for (size_t ivar = 0; ivar < PWN::m_vnames.size(); ivar++) {
                    std::string key = "shell=" + std::to_string(ish)
                                      + " layer=" + std::to_string(il)
                                      + " key=" + PWN::m_vnames[ivar];
                    auto & vec = pwn->getData()[static_cast<PWN::Q>(ivar)];
                    if (!vec.empty()){
                        (*p_log)(LOG_ERR,AT) << " container is not empty\n";
                    }

                    DataSet dataset = file.openDataSet(key);
                    DataType datatype = dataset.getDataType();
                    DataSpace dataspace = dataset.getSpace();
                    const int npts = dataspace.getSimpleExtentNpoints();

                    H5T_class_t classt = datatype.getClass();
                    if ( classt != 1 )
                    {
                        std::cout << key << " is not a float... you can't save this as a float." << std::endl;
                        exit(1);
                    }
                    FloatType ftype = dataset.getFloatType();
                    H5std_string order_string;
                    H5T_order_t order = ftype.getOrder( order_string);
                    size_t size = ftype.getSize();
//                    vec.resizeEachImage(1);
                    double * data = new double[npts];
                    if ( order==0 && size == 4 )
                    {
                        std::cout << "NOTE: This is actually float data. We are casting to double" << std:: endl;
                        dataset.read((double*)data, PredType::IEEE_F32LE); // Our standard integer
                    }
                    else if ( order == 0 && size == 8 )
                        dataset.read(data, PredType::IEEE_F64LE);
                    else if ( order == 1 && size == 4 )
                    {
                        std::cout << "NOTE: This is actually float data. We are casting to double" << std:: endl;
                        dataset.read((double*)data, PredType::IEEE_F32BE);
                    }
                    else if ( order ==1 && size == 8 )
                        dataset.read((double*)data, PredType::IEEE_F64BE);
                    else
                        std::cout << "Did not find data type" << std::endl;
                    std::vector<double> v(data, data + npts);
                    vec = std::move( v );
//                    delete[] data;
                    dataspace.close();
                    datatype.close();
                    dataset.close();

                    if (pwn->getData()[static_cast<PWN::Q>(ivar)].empty() ){
                        std::cout << key << " faild" << std::endl;
                        exit(1);
                    }
                }
                if (pwn->getData()[PWN::iR][0] == 0){
                    (*p_log)(LOG_WARN,AT) << "Loaded not evolved shell [il="<<il<<", "<<"ish="<<ish<<"] \n";
                }
                pwn->i_end_r = pwn->getData()[PWN::iR].size(); // update for eats
//        int i = 1;
            }
        }
        file.close();
//        if ( p_cumShells[0]->getBW(0)->getData()[BW::iR][0] == 0 ){
//            std::cout << p_cumShells[0]->getBW(0)->getData()[BW::iR] << "\n";
//            std::cout << " faild" << std::endl;
//            exit(1);
//        }

#if 0
        LoadH5 ldata;
        ldata.setFileName(workingdir+fname);
        ldata.setVarName("nshells");
        double nshells = ldata.getDoubleAttr("nshells");
        double m_nlayers = ldata.getDoubleAttr("m_nlayers");
        auto & models = getShells();
        for (size_t ish = 0; ish < nshells-1; ish++){
            for (size_t il = 0; il < m_nlayers-1; il++){
                auto & bw = models[il]->getBW(ish);
                for (size_t ivar = 0; ivar < BW::m_vnames.size(); ivar++) {
                    std::string key = "shell=" + std::to_string(ish)
                                    + " layer=" + std::to_string(il)
                                    + " key=" + BW::m_vnames[ivar];
                    ldata.setVarName(BW::m_vnames[ivar]);
                    bw->getData().emplace_back( std::move( ldata.getDataVDouble() ) );
                    (*p_log)(LOG_INFO,AT) << "Reading: "<<key<<"\n";
//                    bw->getData(static_cast<BW::Q>(ivar))
//                        = std::move( ldata.getDataVDouble() );
                }

//                auto & bw = models[il]->getBW(ish);
//                std::string
//                bw->getData()[]
            }
        }
#endif
        (*p_log)(LOG_INFO,AT)<<" dynamics loaded successfully\n";

    }

    void processEvolved(StrDbMap & main_pars, StrStrMap & main_opts){
        if (run_pwn || load_pwn) {
            is_obs_pars_set = true;
            bool lc_freq_to_time = getBoolOpt("lc_use_freq_to_time",main_opts,AT,p_log,false,true);
            lc_freqs = makeVecFromString(getStrOpt("lc_freqs",main_opts,AT,p_log,"",true),p_log);
            lc_times = makeVecFromString(getStrOpt("lc_times",main_opts,AT,p_log,"",true), p_log);
//            Vector spec_freqs = makeVecFromString(getStrOpt("spec_freqs",main_opts,AT,p_log,"",true),p_log);
//            Vector spec_freqs = TOOLS::MakeLogspaceVec(8,16,10);//makeVecFromString(getStrOpt("spec_freqs",main_opts,AT,p_log,"",true),p_log);
            spec_times = makeVecFromString(getStrOpt("spec_times",main_opts,AT,p_log,"",true), p_log);
            spec_gams = makeVecFromString(getStrOpt("spec_gams",main_opts,AT,p_log,"",true),p_log);
            spec_freqs = makeVecFromString(getStrOpt("spec_freqs",main_opts,AT,p_log,"",true),p_log);
//            Vector spec_times = TOOLS::MakeLogspaceVec(1.1,7,400);//makeVecFromString(getStrOpt("spec_times",main_opts,AT,p_log,"",true), p_log);
            skymap_freqs = makeVecFromString(getStrOpt("skymap_freqs",main_opts,AT,p_log,"",true), p_log);
            skymap_times = makeVecFromString(getStrOpt("skymap_times",main_opts,AT,p_log,"",true), p_log);

            /// TODO add checks for non zero sizes

            if (save_pwn)
                savePWNdyn(main_pars);

            if (load_pwn)
                loadPWNDynamics();

            if (do_ele)
                setPreComputePWNAnalyticElectronsPars(workingdir,
                    getStrOpt("fname_e_spec", pwn_opts, AT, p_log, "", true));

            if (do_spec)
                computeSavePWNcomovSpec(workingdir,
                                        getStrOpt("fname_spec", pwn_opts, AT, p_log, "", true),
                                        getStrOpt("fname_spec_layers", pwn_opts, AT, p_log, "", true),
                                        spec_times, spec_freqs, spec_gams,main_pars, pwn_pars, false);
//                setPreComputePWNAnalyticSynchrotronPars(workingdir,
//                    getStrOpt("fname_spec", pwn_opts, AT, p_log, "", true));


            if (do_lc) {
                (*p_log)(LOG_ERR, AT) << " not implemneted. Use methods for ejecta. Do not replicate the code.\n";
                exit(1);
            }
//                computePWNlightcurve(
//                        workingdir,
//                        getStrOpt("fname_light_curve", pwn_opts, AT, p_log, "", true),
//                        getStrOpt("fname_light_curve_layers", pwn_opts, AT, p_log, "", true),
//                        lc_times, lc_freqs, main_pars, pwn_pars, lc_freq_to_time);

            if (do_skymap) {
                (*p_log)(LOG_ERR, AT) << " not implemneted. Use methods for ejecta. Do not replicate the code.\n";
                exit(1);
            }
//                computeSaveSkyImages(
//                        workingdir, getStrOpt("fname_sky_map", pwn_opts, AT, p_log, "", true),
//                        skymap_times, skymap_freqs, main_pars, pwn_pars);
//                    (*p_log)(LOG_INFO, AT) << "jet analytic synch. sky map finished [" << timer.checkPoint() << " s]" << "\n";
        }
    }

private:
    void setPreComputePWNAnalyticElectronsPars(std::string workingdir,std::string fname){
        (*p_log)(LOG_INFO,AT) << "Computing PWN electron dists...\n";
        auto & models = getPWNs();
        for (auto & model : models) {
            model->evalElectronDistribution();
        }
    };
    void setPreComputePWNAnalyticSynchrotronPars(std::string workingdir,std::string fname){
        (*p_log)(LOG_INFO,AT) << "Computing PWN electron dists...\n";
        auto & models = getPWNs();
        for (auto & model : models) {
            model->computeSynchrotronSpectrum();
        }
//        is_synch_computed = true;
    };
    void computeSavePWNcomovSpec(std::string workingdir, std::string fname, std::string fname_shells_layers,
                                 Vector & spec_times, Vector & spec_freqs, Vector & spec_gams,
                                 StrDbMap & main_pars, StrDbMap & ej_pars, bool lc_freq_to_time){

        Vector _times, _freqs;
        cast_times_freqs(spec_times, spec_freqs, _times, _freqs, lc_freq_to_time, p_log);

        (*p_log)(LOG_INFO,AT) << "Computing and saving PWN comoving spectrum...\n";

//        if (!is_synch_computed){
//            std::cerr << " ejecta analytic electrons were not evolved. Cannot computeSynchrotronEmissivityAbsorptionAnalytic light curve (analytic) exiting...\n";
//            std::cerr << AT << " \n";
//            exit(1);
//        }
        if (!is_obs_pars_set){
            std::cerr << " ejecta observer parameters are not set. Cannot computeSynchrotronEmissivityAbsorptionAnalytic light curve (analytic) exiting...\n";
            std::cerr << AT << " \n";
            exit(1);
        }

//        auto & tmp = getShells()[0]->getBW(0)->getSynchAnPtr();

        std::vector< // layers / shells
                std::vector< // options
                        std::vector<double>>> // freqs*times
        out {};

        /// evaluate light curve
        //auto spect =
        evalPWNComovSpectrum(spec_times, spec_freqs, spec_gams);

        /// save total_rad lightcurve
        size_t n = _times.size();
        Vector total_fluxes (n, 0.0);
        for (size_t itnu = 0; itnu < n; ++itnu) {
            size_t ishil = 0;
            for (size_t ishell = 0; ishell < nshells(); ++ishell) {
                for (size_t ilayer = 0; ilayer < nlayers(); ++ilayer) {
                    auto & p_pwn = p_pwns[ilayer];
                    total_fluxes[itnu] += p_pwns[ilayer]->getPars()->m_spectrum[itnu];//spect[ishell][ilayer][itnu];
                    ishil++;
                }
            }
        }
        std::vector<std::string> other_names { "times", "freqs", "total_fluxes" };
        VecVector out_data {_times, _freqs, total_fluxes};

        std::unordered_map<std::string,double> attrs{ {"nshells", nshells()},
                                                      {"nlayers", nlayers()} };
        for (auto& [key, value]: main_pars) { attrs[key] = value; }
        for (auto& [key, value]: ej_pars) { attrs[key] = value; }
        p_out->VectorOfVectorsH5(out_data, other_names, workingdir+fname,  attrs);


        /// save light curve for each shell and layer
        if (fname_shells_layers == "none")
            return;
        std::vector<std::string> group_names;
        VecVector total_fluxes_shell_layer(nshells() * nlayers());
        size_t ii = 0;
        for (size_t ishell = 0; ishell < nshells(); ++ishell) {
            for (size_t ilayer = 0; ilayer < nlayers(); ++ilayer) {
                group_names.emplace_back("shell=" + std::to_string(ishell) + " layer=" + std::to_string(ilayer));
                total_fluxes_shell_layer[ii].resize(n,0.);
                for (size_t ifnu = 0; ifnu < n; ifnu++){
                    total_fluxes_shell_layer[ii][ifnu] = p_pwns[ilayer]->getPars()->m_spectrum[ifnu];//spect[ishell][ilayer][ifnu];
                }
                ii++;
            }
        }
        total_fluxes_shell_layer.emplace_back(_times);
        total_fluxes_shell_layer.emplace_back(_freqs);
        total_fluxes_shell_layer.emplace_back(total_fluxes);

        group_names.emplace_back("times");
        group_names.emplace_back("freqs");
        group_names.emplace_back("total_fluxes");
        p_out->VectorOfVectorsH5(total_fluxes_shell_layer, group_names, workingdir+fname,  attrs);
    }

#if 0
    void computePWNlightcurve(std::string workingdir,std::string fname, std::string fname_shells_layers,
                              Vector lc_times, Vector lc_freqs, StrDbMap & main_pars, StrDbMap & ej_pars,
                              bool lc_freq_to_time){

        Vector _times, _freqs;
        cast_times_freqs(lc_times,lc_freqs,_times,_freqs,lc_freq_to_time,p_log);

        (*p_log)(LOG_INFO,AT) << "Computing and saving PWN light curve with analytic synchrotron...\n";

//        if (!is_synch_computed){
//            std::cerr << " ejecta analytic electrons were not evolved. Cannot evaluateShycnhrotronSpectrum light curve (analytic) exiting...\n";
//            std::cerr << AT << " \n";
//            exit(1);
//        }
        if (!is_obs_pars_set){
            std::cerr << " ejecta observer parameters are not set. Cannot computeSynchrotronEmissivityAbsorptionAnalytic light curve (analytic) exiting...\n";
            std::cerr << AT << " \n";
            exit(1);
        }

//        auto & tmp = getShells()[0]->getBW(0)->getSynchAnPtr();

        std::vector< // layers / shells
                std::vector< // options
                        std::vector<double>>> // freqs*times
        out {};

        /// evaluate light curve
        auto light_curve = evalPWNLightCurves( _times, _freqs );

        /// save total_rad lightcurve
        size_t n = _times.size();
        Vector total_fluxes (n, 0.0);
        for (size_t itnu = 0; itnu < n; ++itnu) {
            size_t ishil = 0;
            for (size_t ishell = 0; ishell < nshells(); ++ishell) {
                for (size_t ilayer = 0; ilayer < nlayers(); ++ilayer) {
                    total_fluxes[itnu] += light_curve[ishell][ilayer][itnu];
                    ishil++;
                }
            }
        }
        std::vector<std::string> other_names { "times", "freqs", "total_fluxes" };
        VecVector out_data {_times, _freqs, total_fluxes};

        std::unordered_map<std::string,double> attrs{ {"nshells", nshells()}, {"nlayers", nlayers()} };
        for (auto& [key, value]: main_pars) { attrs[key] = value; }
        for (auto& [key, value]: ej_pars) { attrs[key] = value; }
        p_out->VectorOfVectorsH5(out_data, other_names, workingdir+fname,  attrs);


        /// save light curve for each shell and layer
        if (fname_shells_layers == "none")
            return;
        std::vector<std::string> group_names;
        VecVector total_fluxes_shell_layer(nshells() * nlayers());
        size_t ii = 0;
        for (size_t ishell = 0; ishell < nshells(); ++ishell) {
            for (size_t ilayer = 0; ilayer < nlayers(); ++ilayer) {
                group_names.emplace_back("shell=" + std::to_string(ishell) + " layer=" + std::to_string(ilayer));
                total_fluxes_shell_layer[ii].resize(n,0.);
                for (size_t ifnu = 0; ifnu < n; ifnu++){
                    total_fluxes_shell_layer[ii][ifnu] = light_curve[ishell][ilayer][ifnu];
                }
                ii++;
            }
        }
        total_fluxes_shell_layer.emplace_back(_times);
        total_fluxes_shell_layer.emplace_back(_freqs);
        total_fluxes_shell_layer.emplace_back(total_fluxes);

        group_names.emplace_back("times");
        group_names.emplace_back("freqs");
        group_names.emplace_back("total_fluxes");
        p_out->VectorOfVectorsH5(total_fluxes_shell_layer, group_names, workingdir+fname,  attrs);
    }
    void computeSaveSkyImages(std::string workingdir, std::string fname, Vector times, Vector freqs,
                              StrDbMap & main_pars, StrDbMap & ej_pars){

        (*p_log)(LOG_INFO,AT) << "Computing and saving PWN sky image with analytic synchrotron...\n";

        size_t nshells_ = nshells();
        size_t nlayers_ = nlayers();


        std::vector< // times & freqs
                std::vector< // v_ns
                        std::vector< // shells
                                std::vector<double>>>> // data
        out {};

        size_t ii = 0;
        out.resize(times.size() * freqs.size());
        for (size_t ifreq = 0; ifreq < freqs.size(); ++ifreq){
            for (size_t it = 0; it < times.size(); ++it){
                out[ii].resize(IMG::m_names.size());
                for (size_t i_vn = 0; i_vn < IMG::m_names.size(); ++i_vn) {
                    out[ii][i_vn].resize(nshells_);
                }
                ii++;
            }
        }

        VecVector other_data{
                times,
                freqs
        };
        ii = 0;
        std::vector<std::string> other_names { "times", "freqs" };
        Images images(nshells_,IMG::m_names.size());

        for (size_t ifreq = 0; ifreq < freqs.size(); ifreq++){
            Vector tota_flux(times.size(), 0.0);
            VecVector total_flux_shell( nshells_ );
            for (auto & total_flux_shel : total_flux_shell)
                total_flux_shel.resize( times.size(), 0.0 );
            for (size_t it = 0; it < times.size(); ++it){
//                auto images = computeEjectaSkyMapPW( times[it],freqs[ifreq]);
//                std::vector<Image> images(nshells_);
                computeSkyMapPieceWise( images, times[it], freqs[ifreq] );
                for (size_t i_vn = 0; i_vn < IMG::m_names.size(); i_vn++) {
                    for (size_t ish = 0; ish < nshells_; ish++) {
                        out[ii][i_vn][ish] = images.getReferenceToTheImage(ish).m_data[i_vn];//arrToVec(images[ish].m_data[i_vn]);
                    }
                }
                for (size_t ish = 0; ish < nshells_; ish++) {
                    tota_flux[it] += images.getReferenceToTheImage(ish).m_f_tot;
                    total_flux_shell[ish][it] = images.getReferenceToTheImage(ish).m_f_tot;
                }
                ii++;
            }
            other_data.emplace_back( tota_flux );
            other_names.emplace_back( "totalflux at freq="+ string_format("%.4e", freqs[ifreq]) );

            for (size_t ish = 0; ish < nshells_; ish++){
                other_data.emplace_back( total_flux_shell[ish] );
                other_names.emplace_back( "totalflux at freq="+ string_format("%.4e", freqs[ifreq]) +
                                          " shell=" + string_format("%d", ish));
            }

        }

        std::vector<std::string> group_names{};
        for (size_t ifreq = 0; ifreq < freqs.size(); ifreq++){
            for (size_t it = 0; it < times.size(); it++){
                group_names.emplace_back("time=" +  string_format("%.4e",times[it])  //std::to_string(times[it])
                                         + " freq=" + string_format("%.4e",freqs[ifreq])); //std::to_string(freqs[ifreq]));
            }
        }

        auto in_group_names = IMG::m_names;//dummy.m_names;

        /// add attributes from model parameters
        std::unordered_map<std::string,double> attrs{
                {"nshells", nshells_},
                {"nshells", nlayers_}
        };

        for (auto& [key, value]: main_pars) { attrs[key] = value; }
        for (auto& [key, value]: ej_pars) { attrs[key] = value; }

        p_out->VectorOfTablesAsGroupsAndVectorOfVectorsH5(workingdir+fname,out,group_names,
                                                          in_group_names,
                                                          other_data,other_names,attrs);

    }
#endif
private:

    void evalPWNComovSpectrum(Vector & times, Vector & freqs, Vector & gams) {
        std::vector<VecVector> spec(nshells()); // [ishell][i_layer][i_time]
        for (auto &arr: spec) {
            size_t n_layers_ej = nlayers();//(p_pars->ej_method_eats == LatStruct::i_pw) ? ejectaStructs.structs[0].nlayers_pw : ejectaStructs.structs[0].nlayers_a ;
            arr.resize(n_layers_ej);
            for (auto &arrr: arr)
                arrr.resize(times.size()*freqs.size(), 0.);

        }
        size_t ii = 0;
        for (size_t ishell = 0; ishell < nshells(); ishell++){
            for (size_t ilayer = 0; ilayer < nlayers(); ilayer++) {
                auto &model = getPWNs()[ilayer];//ejectaModels[ishell][ilayer];
//                model->setEatsPars( pars, opts );
                (*p_log)(LOG_INFO, AT)
                        << " PWN Spec ntimes=" << times.size() << " x nfreqs="<<freqs.size()
                        << " vel_shell=" << ishell << "/" << nshells() - 1
                        << " theta_layer=" << ilayer << "/" << nlayers()
                        << "\n";
                model->evaluateComovingSpectrum(times, freqs, gams);
//                for (size_t k = 0; k < times.size() * freqs.size(); k++)
                    //spec[ishell][ilayer][k] = model->getRad()->getSpectrum()[k];
//                ii ++;
            }
        }
//        return std::move( spec );
    }
#if 0
    std::vector<VecVector> evalPWNLightCurves( Vector & obs_times, Vector & obs_freqs ){
        std::vector<VecVector> light_curves(nshells()); // [ishell][i_layer][i_time]
        for (auto & arr : light_curves){
            size_t n_layers_ej = nlayers();//(p_pars->ej_method_eats == LatStruct::i_pw) ? ejectaStructs.structs[0].nlayers_pw : ejectaStructs.structs[0].nlayers_a ;
            arr.resize(n_layers_ej);
            for (auto & arrr : arr)
                arrr.resize( obs_times.size(), 0. );

        }
        double flux_pj, flux_cj; size_t ii = 0;
//        Image image;
        Image image_i ( ncells(), IMG::m_names.size(), 0, loglevel );
        Image im_pj ( ncells(), IMG::m_names.size(), 0, loglevel );
        Image im_cj ( ncells(), IMG::m_names.size(), 0, loglevel );
        for (size_t ishell = 0; ishell < nshells(); ishell++){
            image_i.clearData();
            im_pj.clearData();
            im_cj.clearData();
//            auto &struc = ejectaStructs.structs[ishell];
//            size_t n_layers_ej = ejectaStructs.structs[ishell].m_nlayers;//(p_pars->ej_method_eats == LatStruct::i_pw) ? struc.nlayers_pw : struc.nlayers_a ;
            for (size_t ilayer = 0; ilayer < nlayers(); ilayer++) {
                auto & model = getPWNs()[ilayer];//ejectaModels[ishell][ilayer];
//                model->setEatsPars( pars, opts );
                if (p_pwns[ilayer]->getPars()->out_spectrum.size() < 1)
                    p_pwns[ilayer]->evaluateComovingSpectrum(spec_times,spec_freqs,spec_gams);

                (*p_log)(LOG_INFO,AT)
                        << " PWN LC ntimes=" << obs_times.size()
                        << " vel_shell=" << ishell << "/" <<nshells()-1
                        << " theta_layer=" << ilayer << "/" << nlayers()
                        << " phi_cells=" << EjectaID2::CellsInLayer(ilayer) << "\n";
                std::vector<VecVector> out;
                model->getEATS()->evalLC_new(
                        p_ej->getId()->method_eats,
                        image_i, im_pj, im_cj, light_curves[ishell][ilayer], obs_times, obs_freqs);
                ii ++;
            }
        }
        return std::move( light_curves );
    }
#endif
    std::vector<VecVector> evalPWNLightCurves( VecVector & out, Vector & obs_times, Vector & obs_freqs ){
        (*p_log)(LOG_ERR,AT) << " Not implemented. Use methods designed for ejecta here, just pass the function for flux calcualtion into it\n";
        exit(1);
        return {{}};
    }
#if 0
    void computeSkyMapPieceWise( Images & images, double obs_time, double obs_freq ){

        size_t nshells_ = nshells();
        size_t nlayers_ = nlayers();
        size_t ncells_ =  (int)ncells();

        if (images.isEmpty()){
            (*p_log)(LOG_ERR,AT) << " isEmpty image passed. Exiting...\n";
            exit(1);
        }
        if (images.size() != nshells_){
            (*p_log)(LOG_ERR,AT) << " number of images does not equal to the number of shells. Exiting...\n";
            exit(1);
        }

        size_t n_jet_empty_images = 0;

        std::vector<std::vector<size_t>> n_empty_images;
        std::vector<size_t> n_empty_images_shells;
        const std::vector<std::string> x {};
        Images tmp (nlayers_, IMG::m_names.size());
        tmp.resizeEachImage(ncells_);
//        for (auto & _tmp : tmp)
//            _tmp.resizeEachImage( ncells_ );
        Image tmp_pj( ncells_, IMG::m_names.size(), 0, loglevel);
        Image tmp_cj( ncells_, IMG::m_names.size(), 0, loglevel);
        for (size_t ishell = 0; ishell < nshells_; ishell++){
//            for (auto & _tmp : tmp)
//                _tmp.clearData();
            tmp.clearEachImage();
            tmp_pj.clearData(); tmp_cj.clearData();
            std::vector<size_t> n_empty_images_layer;
            for (size_t ilayer = 0; ilayer < nlayers_; ilayer++){

                if (p_pwns[ilayer]->getPars()->out_spectrum.size() < 1)
                    p_pwns[ilayer]->evaluateComovingSpectrum(spec_times,spec_freqs,spec_gams);

                (*p_log)(LOG_INFO,AT)
                        << " PWN Skymap time=" << obs_time << " freq="<<obs_freq
                        << " vel_shell=" << ishell << "/" <<nshells()-1
                        << " theta_layer=" << ilayer << "/" << nlayers()
                        << " phi_cells=" << EjectaID2::CellsInLayer(ilayer) << "\n";
                /// Evaluate a given image --------------------------------------
                auto & bw_rad = p_pwns[ilayer]->getEATS();
                bw_rad->evalImagePW(tmp.getReferenceToTheImage(ilayer), tmp_pj, tmp_cj, obs_time, obs_freq);
                /// -------------------------------------------------------------
                if (tmp.getReferenceToTheImage(ilayer).m_f_tot == 0){
                    n_jet_empty_images += 1;
                    n_empty_images_layer.emplace_back(ilayer);
                }
            }
            if(!n_empty_images_layer.empty()){
                n_empty_images_shells.emplace_back(ishell);
                n_empty_images.emplace_back(n_empty_images_layer);
            }
            combineImages(images.getReferenceToTheImage(ishell), ncells_, nlayers_, tmp) ;
        }

        /// print which layers/shells gave isEmpty image
        if (p_log->getLogLevel() == LOG_INFO) {
            if (n_jet_empty_images > 0) {
                auto &ccerr = std::cout;
                ccerr << "PWN at tobs=" << obs_time << " freq=" << obs_freq << " gave an isEmpty images for total_rad n="
                      << n_jet_empty_images << " layers. Specifically:\n";
                for (size_t ish = 0; ish < n_empty_images_shells.size(); ish++) {
//                auto & ejectaStruct = ejectaStructs.structs[n_empty_images_shells[ish]];
//                size_t n_layers_ej = m_nlayers;//(p_pars->ej_method_eats == LatStruct::i_pw) ? ejectaStruct.nlayers_pw : ejectaStruct.nlayers_a ;
                    ccerr << "\t [ishell=" << n_empty_images_shells[ish] << " ilayer] = [ ";
                    for (size_t il = 0; il < n_empty_images[ish].size(); il++) {
                        ccerr << n_empty_images[ish][il] << " ";
                    }
                    ccerr << "] / (" << nlayers_ << " total_rad layers) \n";
                }
            }
        }

//        return std::move( images );
    }
#endif
    void computeSkyMapPieceWise( std::vector<VecVector> & out, double obs_time, double obs_freq ){

        (*p_log)(LOG_ERR,AT) << " Not implemented. Use methods designed for ejecta here, just pass the function for flux calcualtion into it\n";
        exit(1);

    }
};

#endif //SRC_MODEL_MAGNETAR_H
