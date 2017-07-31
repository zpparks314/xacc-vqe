#include "AddUCCSDStatePreparation.hpp"
#include "GateFunction.hpp"
#include "RuntimeOptions.hpp"
#include "JordanWignerIRTransformation.hpp"
#include "ServiceRegistry.hpp"

namespace xacc {
namespace vqe {

std::shared_ptr<IR> AddUCCSDStatePreparation::transform(
		std::shared_ptr<IR> ir) {

	auto runtimeOptions = RuntimeOptions::instance();

	// Create a new GateQIR to hold the spin based terms
	auto newIr = std::make_shared<xacc::quantum::GateQIR>();

	if (!runtimeOptions->exists("n-electrons")) {
		XACCError("To use this State Prep Transformation, you "
				"must specify the number of electrons.");
	}

	if (!runtimeOptions->exists("n-qubits")) {
		XACCError("To use this State Prep Transformation, you "
				"must specify the number of qubits.");
	}

	auto nQubits = std::stoi((*runtimeOptions)["n-qubits"]);
	auto nElectrons = std::stoi((*runtimeOptions)["n-electrons"]);

	// Compute the number of parameters
	auto nOccupied = (int) std::ceil(nElectrons / 2.0);
	auto nVirtual = nQubits / (2 - nOccupied);
	auto nSingle = nOccupied * nVirtual;
	auto nDouble = std::pow(nSingle, 2);
	auto nParams = nSingle + nDouble;

	auto singletIndex = [=](int i, int j) -> int {
	        return i * nOccupied + j;
	};

	auto doubletIndex = [=](int i, int j, int k, int l) -> int {
		return
		(i * nOccupied * nVirtual * nOccupied +
				j * nVirtual * nOccupied +
				k * nOccupied +
				l);
	};

	std::vector<std::string> params;
	for (int i = 0; i < nParams; i++) {
		params.push_back("theta"+std::to_string(i));
	}

	std::cout << "HEY: " << nOccupied << ", " << nVirtual << ", " << nParams << "\n";

	auto kernel = std::make_shared<FermionKernel>("fermiUCCSD");

	for (int i = 0; i < nVirtual; i++) {
		for (int j = 0; j < nOccupied; j++) {
			for (int l = 0; l < 2; l++) {
				std::cout << i << " " << j << " " << l << "\n";
				std::vector<std::pair<int, int>> operators {{ 2 * (i + nOccupied) + l, 1},{2 * j + l, 0}};
				auto fermiInstruction = std::make_shared<FermionInstruction>(operators, params[singletIndex(i,j)]);
				kernel->addInstruction(fermiInstruction);
			}
		}
	}

	for (int i = 0; i < nVirtual; i++) {
		for (int j = 0; j < nOccupied; j++) {
			for (int l = 0; l < 2; l++) {
				for (int i2 = 0; i2 < nVirtual; i2++) {
					for (int j2 = 0; j2 < nOccupied; j2++) {
						for (int l2 = 0; l2 < 2; l2++) {
							std::vector<std::pair<int, int>> operators1 { { 2
									* (i + nOccupied) + l, 1 },
									{ 2 * j + l, 0 }, { 2 * (i2 + nOccupied)
											+ l2, 1 }, { 2 * j2 + l2, 0 } };

							std::vector<std::pair<int, int>> operators2 { { 2
									* j2 + l2, 1 }, { 2 * (i2 + nOccupied) + l2,
									0 }, { 2 * j + l, 1 }, { 2 * (i + nOccupied)
									+ l, 0 } };

							auto doubletIdx1 = nSingle + doubletIndex(i, j, i2, j2);
							// FIXME THIS HAS TO BE NEGATIVE
							auto doubletIdx2 = nSingle + doubletIndex(i, j, i2, j2);

							auto fermiInstruction1 = std::make_shared<
									FermionInstruction>(operators1,
									params[doubletIdx1]);

							kernel->addInstruction(fermiInstruction1);

							auto fermiInstruction2 = std::make_shared<
									FermionInstruction>(operators2,
									params[doubletIdx2]);

							kernel->addInstruction(fermiInstruction2);

						}
					}
				}
			}
		}
	}

	// Create the FermionIR to pass to our transformation.
	auto fermionir = std::make_shared<FermionIR>();
	fermionir->addKernel(kernel);

	std::shared_ptr<IRTransformation> transform;
	if (runtimeOptions->exists("fermion-transformation")) {
		auto transformStr = (*runtimeOptions)["fermion-transformation"];
		transform = ServiceRegistry::instance()->getService<IRTransformation>(
				transformStr);
	} else {
		transform = ServiceRegistry::instance()->getService<IRTransformation>(
				"jordan-wigner");
	}

	// Create the Spin Hamiltonian
	auto transformedIR = transform->transform(fermionir);

	return newIr;
}

}
}

