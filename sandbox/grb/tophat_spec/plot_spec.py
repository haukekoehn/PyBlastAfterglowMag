# import PyBlastAfterglowMag
import numpy as np
import h5py
from pyrsistent import pbag
from scipy.integrate import ode
import matplotlib.pyplot as plt
from scipy import interpolate
# import pytest
from pathlib import Path
from matplotlib.colors import Normalize, LogNorm
from matplotlib import cm
import os
import gc
# try:
#     from PyBlastAfterglowMag.interface import modify_parfile_par_opt
#     from PyBlastAfterglowMag.interface import PyBlastAfterglow
#     from PyBlastAfterglowMag.interface import (distribute_and_run, get_str_val, set_parlists_for_pars)
#     from PyBlastAfterglowMag.utils import latex_float, cgs, get_beta, get_Gamma
#     from PyBlastAfterglowMag.id_maker_analytic import prepare_grb_ej_id_1d, prepare_grb_ej_id_2d
# except ImportError:
#     try:
#         from package.src.PyBlastAfterglowMag.interface import modify_parfile_par_opt
#         from package.src.PyBlastAfterglowMag.interface import PyBlastAfterglow
#         from package.src.PyBlastAfterglowMag.interface import (distribute_and_parallel_run, get_str_val, set_parlists_for_pars)
#         from package.src.PyBlastAfterglowMag.utils import (latex_float, cgs, get_beta, get_Gamma)
#         from package.src.PyBlastAfterglowMag.id_maker_analytic import prepare_grb_ej_id_1d, prepare_grb_ej_id_2d
#     except ImportError:
#         raise ImportError("Cannot import PyBlastAfterglowMag")

# try:
#     import package.src.PyBlastAfterglowMag as PBA
# except ImportError:
#     try:
#         import PyBlastAfterglowMag as PBA
#     except:
#         raise ImportError("Cannot import PyBlastAfterglowMag")
import package.src.PyBlastAfterglowMag as PBA
curdir = os.getcwd()+'/'

def plot_electrons(pba, pba_an):
    # extract data
    spec = pba.GRB.get_lc(key="n_ele_fs", xkey="gams", key_time="times_gams", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    gams = pba.GRB.get_gams(unique=True)
    ts = pba.GRB.get_grid(key="times_gams",spec=True)

    spec_an = pba_an.GRB.get_lc(key="n_ele_fs", xkey="gams", key_time="times_gams", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    gams_an = pba_an.GRB.get_gams(unique=True)

    # load ref.spec from stand alone C++ code
    with h5py.File(os.getcwd()+'/'+'ref_spec.h5','r') as df:
        times_ref = np.array(df["grid_time"],dtype=np.float64)
        gams_ref = np.array(df["grid_gams"],dtype=np.float64)
        spec_ref = np.array(df["matrix_of_N_array"],dtype=np.float64) \
            .reshape(len(times_ref),len(gams_ref))

    # select to plot
    indexes = [10,int(len(ts)/3),2*int(len(ts)/3)]
    colors = ["blue", "green", "red"]

    # plot
    fig, axes = plt.subplots(ncols=2, nrows=2, sharex="all", sharey="row", figsize=(6, 9),
                             gridspec_kw=dict(height_ratios=[1,2]),
                             layout='constrained'
                             )

    ax = axes[0,0]
    for idx, color in zip(indexes, colors):
        ax.plot(gams, spec[idx,:], color=color, linewidth=1.0, linestyle="-")  # , label="$dn/d\gamma|_{\rm num}$")#label=r'$N_{adiab\; losses}$')
        ax.plot(gams, spec_an[idx,:], color=color, linewidth=1.0, linestyle="--")  # , label="$dn/d\gamma|_{\rm num}$")#label=r'$N_{adiab\; losses}$')
        ax.plot(gams_ref, spec_ref[idx,:], color=color, linewidth=1.5, linestyle=":")  # , label="$dn/d\gamma|_{\rm num}$")#label=r'$N_{adiab\; losses}$')
        # ax.axvline(x=gms[idx], ymin=0, ymax=1, color=color, linestyle=':', linewidth=.6, )  # , label=r'$\gamma_{min}$')
        # ax.axvline(x=gcs[idx], ymin=0, ymax=1, color=color, linestyle='-.', linewidth=.6, )  # , label=r'$\gamma_{c}$')

        # ax.plot(gams, matrix_of_N_array_analytical[idx, :], color=color, linewidth=1.0, linestyle=":"),

        # ax.plot(gams, spec[idx,:], color=color, linewidth=1.0, linestyle="-")  # , label="$dn/d\gamma|_{\rm num}$")#label=r'$N_{adiab\; losses}$')


    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_ylim(np.max(spec[0,:])*1e-10, np.max(spec[idx,:])*20)
    ax.set_xlim(.5, gams[-1])
    # plt.plot(gamma, N, color="cyan", linewidth=1.0, linestyle="-", label=r'$N$')

    # ax.set_xlabel(r'$\gamma$')
    ax.set_ylabel(r'$dn/d\gamma$', fontsize=12)
    ax.legend(loc='upper right', prop={'size': 10})  # , bbox_to_anchor=(1.1, 1.1)
    ax.tick_params(direction="in", which="both")
    ax.grid(linestyle=":")


    # normalize spectrum
    spec = spec / np.trapz(y=spec, x=gams, axis=1)[:,np.newaxis]
    spec *= np.power(gams,2)[np.newaxis,:]

    spec_an = spec_an / np.trapz(y=spec_an, x=gams_an, axis=1)[:,np.newaxis]
    spec_an *= np.power(gams_an,2)[np.newaxis,:]

    spec = spec[::4,:]
    spec_an = spec_an[::4,:]
    ts = ts[::4]

    spec[~np.isfinite(spec)] = 1e-100
    norm = LogNorm(vmin=spec.max() * 1e-5, vmax=spec.max())

    # fig, ax = plt.subplots(ncols=1,nrows=1)
    ax = axes[1,0]

    ax.plot(pba.GRB.get_dyn_arr(v_n="gamma_min",ishell=0,ilayer=0),
            pba.GRB.get_dyn_arr(v_n="tburst",ishell=0,ilayer=0),
            color="gray",ls=':')
    ax.plot(pba.GRB.get_dyn_arr(v_n="gamma_c",ishell=0,ilayer=0),
            pba.GRB.get_dyn_arr(v_n="tburst",ishell=0,ilayer=0),
            color="gray",ls='-.')
    ax.plot(pba.GRB.get_dyn_arr(v_n="gamma_max",ishell=0,ilayer=0),
            pba.GRB.get_dyn_arr(v_n="tburst",ishell=0,ilayer=0),
            color="gray",ls='--')

    _c = ax.pcolormesh(gams, ts, spec, cmap='viridis', norm=norm)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_ylabel(r'$t_{\rm burst}$ [s]', fontsize=12)
    ax.set_xlabel(r'$\gamma$', fontsize=12)
    ax.set_xlim(.5, gams[-1])
    # cbar = fig.colorbar(_c, ax=ax, shrink=0.9, pad=.01, label=r'$N_{\rm tot}^{-1} [ \gamma_e^2 dn/d\gamma$ ]')
    # cbar.ax.tick_params(labelsize=12)
    ax.tick_params(direction="in", which="both")

    ax = axes[1,1]

    ax.plot(pba.GRB.get_dyn_arr(v_n="gamma_min",ishell=0,ilayer=0),
            pba.GRB.get_dyn_arr(v_n="tburst",ishell=0,ilayer=0),
            color="gray",ls=':')
    ax.plot(pba.GRB.get_dyn_arr(v_n="gamma_c",ishell=0,ilayer=0),
            pba.GRB.get_dyn_arr(v_n="tburst",ishell=0,ilayer=0),
            color="gray",ls='-.')
    ax.plot(pba.GRB.get_dyn_arr(v_n="gamma_max",ishell=0,ilayer=0),
            pba.GRB.get_dyn_arr(v_n="tburst",ishell=0,ilayer=0),
            color="gray",ls='--')

    _c = ax.pcolormesh(gams_an, ts, spec_an, cmap='viridis', norm=norm)
    ax.set_xscale('log')
    ax.set_yscale('log')
    # ax.set_ylabel(r'$t_{\rm burst}$ [s]', fontsize=12)
    ax.set_xlabel(r'$\gamma$', fontsize=12)
    ax.set_xlim(.5, gams_an[-1])
    cbar = fig.colorbar(_c, ax=ax, shrink=0.9, pad=.01, label=r'$N_{\rm tot}^{-1} [ \gamma_e^2 dn/d\gamma$ ]')
    cbar.ax.tick_params(labelsize=12)
    ax.tick_params(direction="in", which="both")

    ax.legend()
    plt.savefig(os.getcwd()+'/' + "spectra_ele.png",dpi=256)
    plt.show()
    plt.close()

def plot_synchrotron(pba, pba_an):
    spec_syn = pba.GRB.get_lc(key="synch_fs", xkey="freqs", key_time="times_freqs", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    spec_ssc = pba.GRB.get_lc(key="ssc_fs", xkey="freqs", key_time="times_freqs", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    spec_ssa = pba.GRB.get_lc(key="ssa_fs", xkey="freqs", key_time="times_freqs", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    freqs = pba.GRB.get_lc_freqs(unique=True, spec=True)
    ts = pba.GRB.get_grid(key="times_freqs",spec=True)

    spec_syn[~np.isfinite(spec_syn)] = 1e-100
    spec_syn[spec_syn <= 0] = 1e-100
    spec_ssc[~np.isfinite(spec_ssc)] = 1e-100
    spec_ssc[spec_ssc <= 0] = 1e-100
    spec_ssa[~np.isfinite(spec_ssa)] = 1e-100
    spec_ssa[spec_ssa <= 0] = 1e-100

    spec_syn_an = pba_an.GRB.get_lc(key="synch_fs", xkey="freqs", key_time="times_freqs", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    spec_ssa_an = pba_an.GRB.get_lc(key="ssa_fs", xkey="freqs", key_time="times_freqs", freq=None, time=None, ishell=0, ilayer=0, spec=True)
    # freqs = pba.GRB.get_lc_freqs(unique=True, spec=True)
    # ts = pba.GRB.get_grid(key="times_freqs",spec=True)

    spec_syn_an[~np.isfinite(spec_syn_an)] = 1e-100
    spec_syn_an[spec_syn_an <= 0] = 1e-100
    spec_ssa_an[~np.isfinite(spec_ssa_an)] = 1e-100
    spec_ssa_an[spec_ssa_an <= 0] = 1e-100

    indexes = [10,int(len(ts)/3),2*int(len(ts)/3)]
    colors = ["blue", "green", "red"]


    fig, axes = plt.subplots(ncols=2, nrows=4, sharex="all", sharey="row", figsize=(6, 9),
                             gridspec_kw=dict(height_ratios=[1.2, 1.2, 1.7, 1.7]),
                             layout='constrained')

    for idx, color in zip(indexes, colors):
        ax = axes[0, 0]
        ax.plot(freqs, spec_syn[idx, :] * freqs, color=color, linewidth=1.0, linestyle="-")
        ax.plot(freqs, spec_ssc[idx, :] * freqs, color=color, linewidth=1.0, linestyle="-")
        ax.plot(freqs, spec_syn_an[idx, :] * freqs, color=color, linewidth=2.0, linestyle=":")

        ax = axes[1, 0]
        ax.plot(freqs, freqs ** 2 * spec_ssa[idx, :], color=color, linewidth=1.0, linestyle="-")
        ax.plot(freqs, freqs ** 2 * spec_ssa_an[idx, :], color=color, linewidth=2.0, linestyle=":")

    ax = axes[0, 0]
    spec_syn[~np.isfinite(spec_syn)] = 1e-100
    ax.set_ylim(np.max(spec_syn[idx, :] * freqs) * 1.e-15,
                np.max(spec_syn[0, :] * freqs) * 1e1)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_xlim(freqs[0], freqs[-1])
    ax.set_ylabel(r'$\nu j_{\nu}$ [erg/s]')



    ax = axes[1, 0]
    ax.set_ylim(np.max(freqs ** 2 * spec_ssa[idx, :]) * 1.e-15,
                np.max(freqs ** 2 * spec_ssa[idx, :]) * 1e1)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_xlim(freqs[0], freqs[-1])
    ax.set_ylabel(r'$\nu^2 a_{\nu}$')
    ax.legend(loc='upper right', prop={'size': 9})  # , bbox_to_anchor=(1.1, 1.1)

    # -------------------------------

    axes[0, 1].axis("off")
    axes[1, 1].axis("off")

    # ------- plot emissivity -------

    ax = axes[2, 0]  # [1, 1]  # row,col
    spec_syn = spec_syn * freqs[np.newaxis, :]
    norm = LogNorm(vmin=spec_syn.max() * 1e-12, vmax=spec_syn.max() * 10)
    _c = ax.pcolormesh(freqs, ts, spec_syn, cmap='Reds', norm=norm)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_ylabel(r'$t_{\rm burst}$ [s]')
    # ax.set_xlabel(r'$\nu$ [Hz]')
    ax.set_ylim(ts[0], ts[-1])
    # fig.colorbar(_c, ax=ax,shrink=0.9,pad=.01,label=r'$\nu j_{\nu}$ [erg/s]')

    ax = axes[2, 1]  # [1, 1]  # row,col
    spec_syn_an = spec_syn_an * freqs[np.newaxis, :]
    norm = LogNorm(vmin=spec_syn_an.max() * 1e-12, vmax=spec_syn_an.max() * 10)
    _c = ax.pcolormesh(freqs, ts, spec_syn_an, cmap='Reds', norm=norm)
    ax.set_xscale('log')
    ax.set_yscale('log')
    # ax.set_ylabel(r'comoving time [s]')
    # ax.set_xlabel(r'$\nu$ [Hz]')
    ax.set_ylim(ts[0], ts[-1])
    fig.colorbar(_c, ax=ax,shrink=0.9,pad=.01,label=r'$\nu j_{\nu}$ [erg/s]')

    # -------- Plot absorption ------------


    ax = axes[3, 0]  # [1, 1]  # row,col
    spec_ssa = spec_ssa * freqs[np.newaxis, :] ** 2
    norm = LogNorm(vmin=spec_ssa.max() * 1e-7, vmax=spec_ssa.max() * 10)
    _c = ax.pcolormesh(freqs, ts, spec_ssa, cmap='Blues', norm=norm)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_ylabel(r'$t_{\rm burst}$ [s]')
    ax.set_xlabel(r'$\nu$ [Hz]')
    ax.set_ylim(ts[0], ts[-1])
    # fig.colorbar(_c, ax=ax,shrink=0.9,pad=.01,label=r'$\nu^2 a_{\nu}$ [erg/s]')

    ax = axes[3, 1]  # [1, 1]  # row,col
    spec_ssa_an = spec_ssa_an * freqs[np.newaxis, :] ** 2
    norm = LogNorm(vmin=spec_ssa_an.max() * 1e-7, vmax=spec_ssa_an.max() * 10)
    _c = ax.pcolormesh(freqs, ts, spec_ssa_an, cmap='Blues', norm=norm)
    ax.set_xscale('log')
    ax.set_yscale('log')
    # ax.set_ylabel(r'comoving time [s]')
    ax.set_xlabel(r'$\nu$ [Hz]')
    ax.set_ylim(ts[0], ts[-1])
    fig.colorbar(_c, ax=ax,shrink=0.9,pad=.01,label=r'$\nu^2 a_{\nu}$ [erg/s]')

    plt.savefig(os.getcwd()+'/' + "spectra.png",dpi=256)
    plt.show()

def plot_spec():
    gc.collect()
    # prepare initial data (piecewise and adaptive)
    struct = {"struct":"tophat",
              "Eiso_c":1.e53, "Gamma0c": 750., "M0c": -1.,
              "theta_c": np.pi / 10, "theta_w": np.pi / 10}
    pba_id = PBA.id_analytic.JetStruct(n_layers_pw=80, n_layers_a=1)
    # save piece-wise EATS ID
    id_dict, id_pars = pba_id.get_1D_id(pars=struct, type="piece-wise")
    pba_id.save_1d_id(id_dict=id_dict, id_pars=id_pars, outfpath=curdir+"tophat_grb_id_pw.h5")

    # save adaptive EATS ID
    id_dict, id_pars = pba_id.get_1D_id(pars=struct, type="adaptive")
    pba_id.save_1d_id(id_dict=id_dict, id_pars=id_pars, outfpath=curdir+"tophat_grb_id_a.h5")

    i_thetaobs = 0.

    PBA.parfile_tools.modify_parfile_par_opt(workingdir=os.getcwd()+"/", part="main",newpars={"theta_obs":i_thetaobs},newopts={},
                                             parfile="parfile_def.par", newparfile="parfile.par", keep_old=True)
    PBA.parfile_tools.modify_parfile_par_opt(workingdir=os.getcwd()+"/", part="grb",
                                             newpars={"gam1":1,"gam2":1e8,"ngam":250},
                                             newopts={"method_synchrotron_fs":"Dermer09",
                                                      "method_comp_mode":"comovSpec",
                                                      "method_eats":"adaptive",
                                                      "method_ne_fs":"useNe",
                                                      "method_ele_fs":"mix",
                                                      "fname_ejecta_id":"tophat_grb_id_a.h5",
                                                      "fname_spec":"tophat_spec_{}_a.h5",
                                                      "fname_spectrum":"tophat_{}_a.h5"
                                             .format( str(i_thetaobs).replace(".",""))},
                                             parfile="parfile.par", newparfile="parfile.par", keep_old=False)
    pba_an = PBA.interface.PyBlastAfterglow(workingdir=os.getcwd()+"/", parfile="parfile.par")
    pba_an.run(path_to_cpp_executable="/home/vsevolod/Work/GIT/GitHub/PyBlastAfterglowMag/src/pba.out",loglevel="info")

    # ---w

    PBA.parfile_tools.modify_parfile_par_opt(workingdir=os.getcwd()+"/", part="main",newpars={"theta_obs":i_thetaobs},newopts={},
                                             parfile="parfile_def.par", newparfile="parfile.par", keep_old=True)
    PBA.parfile_tools.modify_parfile_par_opt(workingdir=os.getcwd()+"/", part="grb",
                                             newpars={"gam1":1,"gam2":1e8,"ngam":250},
                                             newopts={"method_synchrotron_fs":"Dermer09",
                                                      "method_comp_mode":"comovSpec",
                                                      "method_eats":"adaptive",
                                                      "method_ne_fs":"useNe",
                                                      "method_ele_fs":"numeric",
                                                      "method_ssc_fs":"numeric",
                                                      "fname_ejecta_id":"tophat_grb_id_a.h5",
                                                      "fname_spec":"tophat_spec_{}_num.h5",
                                                      "fname_spectrum":"tophat_{}_num.h5"
                                             .format( str(i_thetaobs).replace(".",""))},
                                             parfile="parfile.par", newparfile="parfile.par", keep_old=False)
    pba = PBA.interface.PyBlastAfterglow(workingdir=os.getcwd()+"/", parfile="parfile.par")
    pba.run(path_to_cpp_executable="/home/vsevolod/Work/GIT/GitHub/PyBlastAfterglowMag/src/pba.out",loglevel="info")

    # --------


    # =========== #

    plot_electrons(pba, pba_an)

    plot_synchrotron(pba, pba_an)



# fig, axes = plt.subplots(nrows=1, ncols=1, figsize=(5.6, 4.2))
    # ax = axes
if __name__ == '__main__':
    plot_spec()