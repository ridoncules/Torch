#include "Thermodynamics.hpp"

#include <bits/forward_list.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <iostream>

#include "Fluid/Fluid.hpp"
#include "Fluid/Grid.hpp"
#include "Fluid/GridCell.hpp"
#include "Fluid/Star.hpp"
#include "MPI/MPI_Wrapper.hpp"
#include "Torch/Common.hpp"
#include "Torch/Constants.hpp"
#include "Torch/Converter.hpp"
#include "Torch/Parameters.hpp"

Thermodynamics::Thermodynamics()
: Integrator("Thermodynamics")
{ }

void Thermodynamics::initialise(std::shared_ptr<Constants> c, ThermoParameters tp) {
	m_consts = std::move(c);

	m_isSubcycling = tp.thermoSubcycling;
	m_thermoHII_Switch = tp.thermoHII_Switch;
	m_heatingAmplification = tp.heatingAmplification;
	m_massFractionH = tp.massFractionH;
	m_minTempInitialState = tp.minTempInitialState;

	m_z0 = 5.0e-4;
	m_excessEnergy = m_consts->converter.toCodeUnits(m_consts->converter.EV_2_ERGS(5), 1, 2, -2);
	m_T1 = 33610;
	m_T2 = 2180;
	m_T3 = 28390;
	m_T4 = 1780;
	m_imlc = m_consts->converter.toCodeUnits(2.905e-19, 1.0, 5.0, -3.0);
	m_nmlc = m_consts->converter.toCodeUnits(4.477e-20, 1.0, 5.0, -3.0);
	m_ciec_minT = 5.0e4;
	m_ciec = m_consts->converter.toCodeUnits(3.485e-15, 1.0, 5.0, -3.0);
	m_cxhi_damp = 5.0e5;
	m_n0 = m_consts->converter.toCodeUnits(1.0e6, 0.0, -3.0, 0.0);
	m_nmc = m_consts->converter.toCodeUnits(3.981e-27, 1.0, 3.8, -3.0);
	m_fuvh_a = m_consts->converter.toCodeUnits(1.9e-26, 1.0, 4.0, -2.0);
	m_fuvh_b = m_consts->converter.toCodeUnits(1.00000, 0.0, 0.0, -1.0);
	m_fuvh_c = m_consts->converter.toCodeUnits(6.40000, 0.0, -1.0, 0.0);
	m_irh_a = m_consts->converter.toCodeUnits(7.7e-32, 1.0, 4.0, -2.0);
	m_irh_b = m_consts->converter.toCodeUnits(3.0e4, 0.0, -3.0, 0.0);
	m_crh = m_consts->converter.toCodeUnits(5.0e-27, 1.0, 2.0, -3.0);
	m_T_min = 100;
	m_T_soft = 300;

	initCollisionalExcitationHI(m_consts->converter);
	initRecombinationHII(m_consts->converter);
}

void Thermodynamics::initCollisionalExcitationHI(const Converter& converter) {

	double T[26] = {3162.2776602, 3981.0717055, 5011.8723363, 6309.5734448, 7943.2823472,
			10000.0000000, 12589.2541179, 15848.9319246, 19952.6231497, 25118.8643151,
			31622.7766017, 39810.7170553, 50118.7233627, 63095.7344480, 79432.8234724,
			100000.0000000, 125892.5411794, 158489.3192461, 199526.2314969, 251188.6431510,
			316227.7660168, 398107.1705535, 501187.2336273, 630957.3444802, 794328.2347243,
			1000000.0000000};

	double R[26] = {1.150800e-34, 2.312065e-31, 9.571941e-29, 1.132400e-26,	4.954502e-25,
			9.794900e-24, 1.035142e-22, 6.652732e-22, 2.870781e-21, 9.036495e-21, 2.218196e-20,
			4.456562e-20, 7.655966e-20, 1.158777e-19, 1.588547e-19, 2.013724e-19, 2.393316e-19,
			2.710192e-19, 2.944422e-19, 3.104560e-19, 3.191538e-19, 3.213661e-19, 3.191538e-19,
			3.126079e-19, 3.033891e-19, 2.917427e-19};

	std::vector<std::pair<double, double>> dataPairs;
	for (int i = 0; i < 26; i++) {
		dataPairs.push_back(std::make_pair(std::log10(T[i]), std::log10(converter.toCodeUnits(R[i], 1, 5, -3))));
		//dataPairs.push_back(std::make_pair(T[i], converter.toCodeUnits(R[i], 1, 5, -3)));
	}
	m_collisionalExcitationHI_CoolingRates = std::unique_ptr<LogSplineData>(new LogSplineData(dataPairs));
}


/**
 * @brief Cubic spline fit for Hummer (1994) HII recombination cooling rate data.
 */
void Thermodynamics::initRecombinationHII(const Converter& converter) {
	double coolb[31] = {8.287e-11, 7.821e-11, 7.356e-11, 6.892e-11, 6.430e-11, 5.971e-11,
					5.515e-11, 5.062e-11, 4.614e-11, 4.170e-11, 3.734e-11, 3.306e-11, 2.888e-11,
					2.484e-11, 2.098e-11, 1.736e-11, 1.402e-11, 1.103e-11, 8.442e-12, 6.279e-12,
					4.539e-12, 3.192e-12, 2.185e-12, 1.458e-12, 9.484e-13, 6.023e-13, 3.738e-13,
					2.268e-13, 1.348e-13, 7.859e-14, 4.499e-14};

	std::vector<std::pair<double, double>> dataPairs;
	for (int i = 0; i < 26; i++) {
		double T = std::exp(std::log(10.0)*(1.0 + 0.2*static_cast<double>(i)));
		double R = converter.toCodeUnits(coolb[i]/std::sqrt(T), 0, 3, -1);
		dataPairs.push_back(std::make_pair(T, R));
	}
	m_recombinationHII_CoolingRates = std::unique_ptr<LinearSplineData>(new LinearSplineData(dataPairs));
}

void Thermodynamics::initialiseMinTempField(Fluid& fluid) const {
	if (m_minTempInitialState) {
		for (GridCell& cell : fluid.getGrid().getIterable("GridCells"))
			cell.T_min = fluid.calcTemperature(cell.Q[UID::HII], cell.Q[UID::PRE], cell.Q[UID::DEN]);
	}
	else {
		for (GridCell& cell : fluid.getGrid().getIterable("GridCells"))
			cell.T_min = m_T_min;
	}
}

double Thermodynamics::fluxFUV(const double Q_FUV, const double dist_sqrd) const {
	if (dist_sqrd != 0)
		return Q_FUV/(1.2e7*4*m_consts->pi*dist_sqrd);
	else
		return 0;
}

//Ionised metal line cooling (Henney et al. 2009, eq. A9).
double Thermodynamics::ionisedMetalLineCooling(const double ne, const double T) const {
	return m_imlc*m_z0*ne*ne*std::exp(-m_T1/T - (m_T2/T)*(m_T2/T));
}

//Neutral metal line cooling (Henney et al. 2009, eq. A10)
double Thermodynamics::neutralMetalLineCooling(const double ne, const double nn, const double T) const {
	return m_nmlc*m_z0*ne*nn*std::exp(-m_T3/T - (m_T4/T)*(m_T4/T));
}

//Collisional ionization equilibrium-cooling curve (Henney et al. 2009, eq. A13).
double Thermodynamics::collisionalIonisationEquilibriumCooling(const double ne, const double T) const {
	if (T > m_ciec_minT) {
		double cie_rate = m_ciec*ne*ne*m_z0*std::exp(-0.63*std::log(T))*(1.0-std::exp(-std::pow(1.0e-5*T, 1.63)));
		double smoothing = std::min(1.0, (T-5.0e4)/(2.0e4)); //linear smoothing spread over 20000K (PION: cooling.cc).
		return cie_rate*smoothing;
	}
	else
		return 0;
}

//Neutral and molecular cooling from cloudy models (Henney et al. 2009, eq. A14).
double Thermodynamics::neutralMolecularLineCooling(const double nH, const double HIIFRAC, const double T) const {
	double T0 = 70.0 + 220.0*std::pow(nH/m_n0, 0.2);
	return m_nmc*(1.0-HIIFRAC)*(1.0-HIIFRAC)*std::pow(nH, 1.6)*std::sqrt(T)*std::exp(-T0/T);
}

/**
 * @brief Cubic spline interpolation of the collisional excitation cooling rate of HI.
 * The spline is fit in log-log space, and the slopes off the end of the fit are also logarithmic, so we take the log of T, get
 * log10(rate), and then return exp10() of the rate.
 * @param nH Hydrogen number density.
 * @param HIIFRAC Fraction of hydrogen gas that is ionised.
 * @param T Gas temperature.
 * @return Collisional excitation cooling rate of HI.
 */
double Thermodynamics::collisionalExcitationHI(const double nH, const double HIIFRAC, const double T) const {
	double rate = m_collisionalExcitationHI_CoolingRates->interpolate(std::log10(T));

	return HIIFRAC*(1.0-HIIFRAC)*nH*nH*std::exp((2.302585093*rate)-((T/m_cxhi_damp)*(T/m_cxhi_damp)));
	//return HIIFRAC*(1.0-HIIFRAC)*nH*nH*rate*std::exp(-(T/m_cxhi_damp)*(T/m_cxhi_damp));
}

/**
 * Cubic spline interpolation of the recombination cooling rate of HII.
 * Free–free and free–bound transitions of ionised hydrogen (Henney et al. 2009, eq. A11).
 * @param nH Hydrogen number density.
 * @param HIIFRAC Fraction of hydrogen gas that is ionised.
 * @param T Gas temperature.
 * @return Recombination cooling rate of HII.
 */
double Thermodynamics::recombinationHII(const double nH, const double HIIFRAC, const double T) const {
	double rate = m_recombinationHII_CoolingRates->interpolate(T);

	return HIIFRAC*HIIFRAC*nH*nH*m_consts->boltzmannConst*T*rate;
}

//FUV heating (Henney et al. 2009, eq. A3)
double Thermodynamics::farUltraVioletHeating(const double nH, const double Av_FUV, const double F_FUV) const {
	return m_fuvh_a*nH*F_FUV*std::exp(-1.9*Av_FUV)/(m_fuvh_b +m_fuvh_c*F_FUV*std::exp(-1.9*Av_FUV)/nH);
}

//IR Heating (Henney et al. 2009, eq. A6)
double Thermodynamics::infraRedHeating(const double nH, const double Av_FUV, const double F_FUV) const {
	return m_irh_a*nH*F_FUV*std::exp(-0.05*Av_FUV)*std::exp(-2.0*log(1.0+m_irh_b/nH));
}

//Cosmic Ray Heating (Henney et al. 2009, eq. A7)
//Hack: Increasing this by 10X to compensate for no X-ray heating.
double Thermodynamics::cosmicRayHeating(const double nH) const {
	return m_crh*nH;
}

//"Soft landing" to equilibrium neutral gas temperature
double Thermodynamics::softLanding(const double rate, const double T, const double T_min) const {
	double result = rate;
	if (rate < 0.0) {
		if (T <= T_min)
			result = 0;
		else if (T <= T_min + 200)
			result = rate*(T-T_min)/200;
	}
	return result;
}

/**
 * @brief Calculates the cooling and heating rates of gas in a grid cell due to atomic processes.
 *
 * Cooling due to collisionally excited optical lines of ionised metals; collisionally
 * excited lines of neutral metals; free-free and free-bound transitions of ionized
 * hydrogen; collisionally excited lines of neutral hydrogen; collisional ionisation
 * equilibrium-cooling; and CLOUDY PDR models.
 * Heating due to ionizing EUV photons; absorption of FUV radiation by dust grains;
 * hard X-rays deep inside the PDR; stellar radiation reprocessed by dense gas (>10^4cm-3)
 * and absorbed by dust; and cosmic ray particles.
 *
 * @param fluid The fluid.
 */
void Thermodynamics::preTimeStepCalculations(Fluid& fluid) const {
	if (fluid.getStar().on)
		rayTrace(fluid);

	Grid& grid = fluid.getGrid();

	for (int cellID : grid.getOrderedIndices("CausalNonWind")) {
		GridCell& cell = grid.getCell(cellID);

		if (cell.Q[UID::ADV] < m_thermoHII_Switch) {
			cell.T[TID::RATE] = 0;
			continue;
		}
		double nH = m_massFractionH*cell.Q[UID::DEN] / m_consts->hydrogenMass;
		double HIIFRAC = cell.Q[UID::HII];
		double ne = nH*(HIIFRAC); //Ionised hydrogen no. density.
		double nn = nH*(1.0-HIIFRAC); //Neutral hydrogen no. density.
		double T = fluid.calcTemperature(cell.Q[UID::HII], cell.Q[UID::PRE], cell.Q[UID::DEN]);

		double rsqrd = 0;
		double F_FUV = 0;
		if (fluid.getStar().on) {
			for (int id = 0; id < m_consts->nd; ++id)
				rsqrd += (cell.xc[id]-fluid.getStar().xc[id])*(cell.xc[id]-fluid.getStar().xc[id])*grid.dx[id]*grid.dx[id];
			F_FUV = fluxFUV(0.5*fluid.getStar().photonRate, rsqrd);
		}
		double tau = cell.T[TID::COL_DEN];
		double Av_FUV = 1.086*m_consts->dustExtinctionCrossSection*tau; //!< Visual band optical extinction in magnitudes.

		double rate = 0;

		rate += farUltraVioletHeating(nH, Av_FUV, F_FUV);
		rate += infraRedHeating(nH, Av_FUV, F_FUV);
		rate += cosmicRayHeating(nH);

		cell.T[TID::HEAT] = rate;

		rate -= ionisedMetalLineCooling(ne, T);
		rate -= neutralMetalLineCooling(ne, nn, T);
		rate -= collisionalExcitationHI(nH, HIIFRAC, T);
		rate -= collisionalIonisationEquilibriumCooling(ne, T);
		rate -= neutralMolecularLineCooling(nH, HIIFRAC, T);
		rate = softLanding(rate, T, cell.T_min);

		cell.T[TID::RATE] = m_heatingAmplification*rate;
	}
}

void Thermodynamics::integrate(double dt, Fluid& fluid) const {
	if (!m_isSubcycling)
		return;

	Grid& grid = fluid.getGrid();

	for (int cellID : grid.getOrderedIndices("CausalNonWind")) {
		GridCell& cell = grid.getCell(cellID);

		if (cell.Q[UID::ADV] < m_thermoHII_Switch) {
			for (int i = 0; i < HID::N; ++i)
				cell.H[i] = 0;
			cell.T[TID::RATE] = 0;
			continue;
		}
		double nH = m_massFractionH*cell.Q[UID::DEN] / m_consts->hydrogenMass;
		double HIIFRAC = cell.Q[UID::HII];
		double ne = nH*(HIIFRAC); //Ionised hydrogen no. density.
		double nn = nH*(1.0-HIIFRAC); //Neutral hydrogen no. density.

		double dti = std::abs(0.10*cell.U[UID::PRE] / cell.T[TID::RATE]);

		// Pressure changes over subcycle therefore temperature does, affecting cooling rate.
		double mu_inv = m_massFractionH*(cell.Q[UID::HII] + 1.0) + (1.0 - m_massFractionH)*0.25;
		double pre2temp = 1.0/(mu_inv*m_consts->specificGasConstant*cell.Q[UID::DEN]);
		double temp2pre = (mu_inv*m_consts->specificGasConstant*cell.Q[UID::DEN]);
		double rate2dpre = std::min(dt, dti)*(cell.heatCapacityRatio - 1.0);
		double dpre2rate = 1.0/rate2dpre;

		double pressure = cell.Q[UID::PRE] + cell.T[TID::RATE] * rate2dpre;
		double subcycleT = pressure*pre2temp;
		// Fix pressure and temperature and heating rate.
		if (pressure < m_consts->pfloor || subcycleT < cell.T_min) {
			double pfloor = std::max(cell.T_min*temp2pre, m_consts->pfloor);
			subcycleT = pfloor*pre2temp;
			pressure = pfloor;
		}

		if (dt > dti) {
			double dtdti = dt/dti;
			// Number of subcycle steps and cooling step.
			int nsteps = dtdti - (int)dtdti > 0 ? (int)(dtdti + 1.0) : (int)(dtdti + 0.5);
			dti = dt / nsteps;
			// A step has already been made.
			--nsteps;

			// Subcycling.
			for (int i = 0; i < nsteps; ++i) {
				double subcycleRate = cell.T[TID::HEAT];
				subcycleRate -= ionisedMetalLineCooling(ne, subcycleT);
				subcycleRate -= neutralMetalLineCooling(ne, nn, subcycleT);
				subcycleRate -= collisionalExcitationHI(nH, HIIFRAC, subcycleT);
				subcycleRate -= collisionalIonisationEquilibriumCooling(ne, subcycleT);
				subcycleRate -= neutralMolecularLineCooling(nH, HIIFRAC, subcycleT);
				subcycleRate = m_heatingAmplification*softLanding(subcycleRate, subcycleT, cell.T_min);

				// Update pressure and total heating rate.
				pressure += subcycleRate*rate2dpre;
				subcycleT = pressure*pre2temp;
				// Fix pressure and temperature and heating rate.
				if (pressure < m_consts->pfloor || subcycleT < cell.T_min) {
					double pfloor = std::max(cell.T_min*temp2pre, m_consts->pfloor);
					subcycleT = pfloor*pre2temp;
					pressure = pfloor;
				}
			}
		}

		cell.T[TID::RATE] = (pressure - cell.Q[UID::PRE]) * dpre2rate;
		cell.H[HID::TOT] = cell.T[TID::RATE];
	}
}

void Thermodynamics::updateColDen(GridCell& cell, Fluid& fluid, const double dist2) const {
	Grid& grid = fluid.getGrid();
	if (dist2 > 0.95*0.95) {
		double colden[4] = {0.0, 0.0, 0.0, 0.0};
		double w_raga[4];
		for(int i = 0; i < 4; ++i) {
			if (cell.neighbourIDs[i] != -1)
				colden[i] = grid.getCell(cell.neighbourIDs[i]).T[TID::COL_DEN]+grid.getCell(cell.neighbourIDs[i]).T[TID::DCOL_DEN];
			w_raga[i] = colden[i] == 0 ? 0 : cell.neighbourWeights[i]/colden[i];
		}
		double sum_w = w_raga[0]+w_raga[1]+w_raga[2]+w_raga[3];

		double newcolden = 0.0;
		for (int i = 0; i < 4 && sum_w != 0; ++i) {
			w_raga[i] = w_raga[i]/sum_w;
			newcolden += w_raga[i]*colden[i];
		}
		cell.T[TID::COL_DEN] = newcolden;
		cell.T[TID::DCOL_DEN] = (cell.Q[UID::DEN] / m_consts->hydrogenMass)*cell.ds;
	}
	else {
		cell.T[TID::COL_DEN] = 0;
		cell.T[TID::DCOL_DEN] = (cell.Q[UID::DEN] / m_consts->hydrogenMass)*cell.ds;
	}
}

void Thermodynamics::rayTrace(Fluid& fluid) const {
	MPIW& mpihandler = MPIW::Instance();
	Grid& grid = fluid.getGrid();
	Star& star = fluid.getStar();
	PartitionManager& partition = grid.getPartitionManager();
	partition.resetBuffer();

	if (fluid.getStar().core != Star::Location::HERE) {
		int source = (star.core == Star::Location::LEFT) ? mpihandler.rank - 1 : mpihandler.rank + 1;
		partition.recvData(source, SendID::THERMO_MSG);

		for (GridCell& ghost : grid.getIterable(star.core == Star::Location::LEFT ? "LeftPartitionCells" : "RightPartitionCells")) {
			ghost.T[TID::COL_DEN] = partition.getRecvItem();
			ghost.T[TID::DCOL_DEN] = partition.getRecvItem();
		}
	}
	for (int cellID : grid.getOrderedIndices("CausalWind")) {
		GridCell& cell = grid.getCell(cellID);

		double dist2 = 0;
		for (int i = 0; i < m_consts->nd; ++i)
			dist2 += (cell.xc[i] - fluid.getStar().xc[i])*(cell.xc[i] - fluid.getStar().xc[i]);
		updateColDen(cell, fluid, dist2);
	}
	for (int cellID : grid.getOrderedIndices("CausalNonWind")) {
		GridCell& cell = grid.getCell(cellID);

		double dist2 = 0;
		for (int i = 0; i < m_consts->nd; ++i)
			dist2 += (cell.xc[i] - fluid.getStar().xc[i])*(cell.xc[i] - fluid.getStar().xc[i]);
		updateColDen(cell, fluid, dist2);
	}
	// Send column densities to processor on left.
	if(!(mpihandler.getRank() == 0 || fluid.getStar().core == Star::Location::LEFT)) {
		for (GridCell& ghost : grid.getIterable("LeftPartitionCells")) {
			GridCell& cell = grid.right(0, ghost);
			partition.addSendItem(cell.T[TID::COL_DEN]);
			partition.addSendItem(cell.T[TID::DCOL_DEN]);
		}
		int destination = mpihandler.rank - 1;
		partition.sendData(destination, SendID::THERMO_MSG);
	}
	// Send column densities to processor on right.
	if(!(mpihandler.getRank() == mpihandler.nProcessors()-1 || fluid.getStar().core == Star::Location::RIGHT)) {
		for (GridCell& ghost : grid.getIterable("RightPartitionCells")) {
			GridCell& cell = grid.left(0, ghost);
			partition.addSendItem(cell.T[TID::COL_DEN]);
			partition.addSendItem(cell.T[TID::DCOL_DEN]);
		}
		int destination = mpihandler.rank + 1;
		partition.sendData(destination, SendID::THERMO_MSG);
	}
}

void Thermodynamics::fillHeatingArrays(Fluid& fluid) {
	if (fluid.getStar().on)
		rayTrace(fluid);

	Grid& grid = fluid.getGrid();

	for (int cellID : grid.getOrderedIndices("CausalNonWind")) {
		GridCell& cell = grid.getCell(cellID);

		if (cell.Q[UID::ADV] < m_thermoHII_Switch) {
			for (int i = 0; i < HID::N; ++i)
				cell.H[i] = 0;
			continue;
		}

		double nH = m_massFractionH*cell.Q[UID::DEN] / m_consts->hydrogenMass;
		double HIIFRAC = cell.Q[UID::HII];
		double ne = HIIFRAC*nH;
		double nn = (1.0 - HIIFRAC)*nH;
		double T = cell.temperature(m_massFractionH, m_consts->specificGasConstant);
		double rsqrd = 0;
		double F_FUV = 0;
		if (fluid.getStar().on) {
			for (int id = 0; id < m_consts->nd; ++id)
				rsqrd += (cell.xc[id]-fluid.getStar().xc[id])*(cell.xc[id]-fluid.getStar().xc[id])*grid.dx[id]*grid.dx[id];
			F_FUV = fluxFUV(0.5*fluid.getStar().photonRate, rsqrd);
		}
		double tau = cell.T[TID::COL_DEN];
		double Av_FUV = 1.086*m_consts->dustExtinctionCrossSection*tau; //!< Visual band optical extinction in magnitudes.

		cell.H[HID::FUVH] = farUltraVioletHeating(nH, Av_FUV, F_FUV);
		cell.H[HID::IRH] = infraRedHeating(nH, Av_FUV, F_FUV);
		cell.H[HID::CRH] = cosmicRayHeating(nH);

		cell.H[HID::IMLC] = -ionisedMetalLineCooling(ne, T);
		cell.H[HID::NMLC] = -neutralMetalLineCooling(ne, nn, T);
		cell.H[HID::CEHI] = -collisionalExcitationHI(nH, HIIFRAC, T);
		cell.H[HID::CIEC] = -collisionalIonisationEquilibriumCooling(ne, T);
		cell.H[HID::NMC] = -neutralMolecularLineCooling(nH, HIIFRAC, T);

		cell.H[HID::TOT] += cell.H[HID::RHII] + cell.H[HID::EUVH];
	}
}

double Thermodynamics::calculateTimeStep(double dt_max, Fluid& fluid) const {
	Grid& grid = fluid.getGrid();
	double dt = dt_max;
	for (GridCell& cell : grid.getCells()) {
		if (cell.T[TID::RATE] != 0) {
			double frac = m_isSubcycling ? 1.0 : 0.1;
			double dti = std::abs(frac*cell.U[UID::PRE]/cell.T[TID::RATE]);
			if (dti < dt) {
				dt = dti;
			}
		}
	}
	return dt;
}

void Thermodynamics::updateSourceTerms(double dt, Fluid& fluid) const {
	Grid& grid = fluid.getGrid();
	for (int cellID : grid.getOrderedIndices("CausalNonWind")) {
		GridCell& cell = grid.getCell(cellID);
		cell.UDOT[UID::PRE] += cell.T[TID::RATE];
		cell.T[TID::RATE] = cell.T[TID::HEAT] = 0;
	}
}
