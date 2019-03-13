#include "RDMPurificationDecorator.hpp"
#include "FermionCompiler.hpp"
#include "IRProvider.hpp"
#include "InstructionIterator.hpp"
#include "PauliOperator.hpp"
#include "RDMGenerator.hpp"
#include "XACC.hpp"
#include <unsupported/Eigen/CXX11/TensorSymmetry>

namespace xacc {
namespace vqe {
using IP = Eigen::IndexPair<int>;
using T2 = Eigen::Tensor<std::complex<double>, 2>;
using T4 = Eigen::Tensor<std::complex<double>, 4>;
using T0 = Eigen::Tensor<std::complex<double>, 0>;

void RDMPurificationDecorator::execute(
    std::shared_ptr<AcceleratorBuffer> buffer,
    const std::shared_ptr<Function> function) {

  if (!decoratedAccelerator) {
    xacc::error("Null Decorated Accelerator Error");
  }

  return;
}

std::vector<std::shared_ptr<AcceleratorBuffer>>
RDMPurificationDecorator::execute(
    std::shared_ptr<AcceleratorBuffer> buffer,
    const std::vector<std::shared_ptr<Function>> functions) {

  std::vector<std::shared_ptr<AcceleratorBuffer>> buffers;
  if (!decoratedAccelerator) {
    xacc::error("RDMPurificationDecorator - Null Decorated Accelerator Error");
  }

  // Here I expect the ansatz to be functions[0]->getInstruction(0);
  auto ansatz =
      std::dynamic_pointer_cast<Function>(functions[0]->getInstruction(0));
  if (!ansatz)
    xacc::error("RDMPurificationDecorator - Ansatz was null.");

  auto nQubits = buffer->size();
  std::vector<int> qubitMap(nQubits) ;
  std::iota (std::begin(qubitMap), std::end(qubitMap), 0);

  // optionally map the ansatz to a
  // different set of physical qubits
  if (xacc::optionExists("rdm-qubit-map")) {
    auto mapStr = xacc::getOption("rdm-qubit-map");

    std::vector<std::string> split;
    boost::split(split, mapStr, boost::is_any_of(","));

    qubitMap.clear();
    for (auto s : split) {
      auto idx = std::stoi(s);
      qubitMap.push_back(idx);
    }
    ansatz = ansatz->enabledView();
    ansatz->mapBits(qubitMap);
  }

  auto src = xacc::getOption("rdm-source");

  // Get hpq, hpqrs
  xacc::setOption("no-fermion-transformation", "");
  auto c = xacc::getCompiler("fermion");
  auto ir = c->compile(src, decoratedAccelerator);
  auto fk =
      std::dynamic_pointer_cast<FermionKernel>(ir->getKernels()[0]);
  xacc::unsetOption("no-fermion-transformation");

  auto energy = fk->E_nuc();
  auto hpq = fk->hpq(nQubits);
  auto hpqrs = fk->hpqrs(nQubits);
  RDMGenerator generator(buffer->size(), decoratedAccelerator, hpq, hpqrs);

  buffers = generator.generate(ansatz, qubitMap);

  //   auto rho_pq = generator.rho_pq;
  T4 rho_pqrs = generator.rho_pqrs;
  Eigen::Tensor<double,4> realt = rho_pqrs.real();
  auto real = realt.data();
  std::vector<double> rho_pqrs_data(real, real + rho_pqrs.size());

  double bad_energy = energy;
  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      for (int r = 0; r < nQubits; r++) {
        for (int s = 0; s < nQubits; s++) {
          bad_energy +=
              0.5 * std::real(hpqrs(p, q, r, s) *
                              (rho_pqrs(p, q, s, r) + rho_pqrs(r, s, q, p)));
        }
      }
    }
  }

  Eigen::array<int, 2> cc2({1, 3});
  Eigen::DynamicSGroup rho_pqrs_Sym;
  rho_pqrs_Sym.addAntiSymmetry(0, 1);
  rho_pqrs_Sym.addAntiSymmetry(2, 3);

  T2 bad_rhopq_tensor = rho_pqrs.trace(cc2);
  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      bad_energy += 0.5 * std::real(hpq(p, q) * (bad_rhopq_tensor(p, q) +
                                                 bad_rhopq_tensor(q, p)));
    }
  }

  xacc::info("Non-purified Energy: " + std::to_string(bad_energy));

  xacc::info("Filtering 2-RDM");
  Eigen::Tensor<std::complex<double>, 4> filtered_rhopqrs(nQubits, nQubits,
                                                          nQubits, nQubits);
  filtered_rhopqrs.setZero();

  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      for (int r = 0; r < nQubits; r++) {
        for (int s = 0; s < nQubits; s++) {
          if ((p <= q) && (r <= s)) {
            filtered_rhopqrs(p, q, r, s) = rho_pqrs(p, q, r, s);
            // filtered_rhopqrs(r, s, p, q) = rho_pqrs(p, q, r, s);
          }
        }
      }
    }
  }

  T4 rdm = filtered_rhopqrs;

  T4 rdmSq = rdm.contract(rdm, Eigen::array<IP, 2>{IP(2, 0), IP(3, 1)});
  T4 diff = rdmSq - rdm;
  T4 diffSq = diff.contract(diff, Eigen::array<IP, 2>{IP(2, 0), IP(3, 1)});

  T0 tmp = diffSq.trace();
  //   double tr_diff_sq = std::real(tmp(0));

  double tr_diff_sq = 0.0;
  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      tr_diff_sq += std::real(diffSq(p, q, p, q));
    }
  }

  std::stringstream ss;
  ss << std::setprecision(8) << tr_diff_sq;
  xacc::info("diffsq_pqrs trace: " + ss.str());

  int count = 0;
  while (tr_diff_sq > 1e-8) {
    rdm = rdm.constant(3.) * rdmSq -
          rdm.constant(2.) *
              rdm.contract(rdmSq, Eigen::array<IP, 2>{IP(2, 0), IP(3, 1)});

    // Update
    rdmSq = rdm.contract(rdm, Eigen::array<IP, 2>{IP(2, 0), IP(3, 1)});
    diff = rdmSq - rdm;
    diffSq = diff.contract(diff, Eigen::array<IP, 2>{IP(2, 0), IP(3, 1)});

    tr_diff_sq = 0.0;
    for (int p = 0; p < nQubits; p++) {
      for (int q = 0; q < nQubits; q++) {
        tr_diff_sq += std::real(diffSq(p, q, p, q));
      }
    }

    std::stringstream sss;
    sss << std::setprecision(8) << tr_diff_sq;
    xacc::info("Iter: " + std::to_string(count) +
               ", diffsq_pqrs trace: " + sss.str());
    count++;
  }

  // reconstruct rhopqrs using symmetry rules
  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      for (int r = 0; r < nQubits; r++) {
        for (int s = 0; s < nQubits; s++) {
          rho_pqrs_Sym(rdm, p, q, r, s) = rdm(p, q, r, s);
          rho_pqrs_Sym(rdm, r, s, p, q) = rdm(p, q, r, s);
        }
      }
    }
  }

  realt = rdm.real();
  real = realt.data();
  std::vector<double> fixed_rho_pqrs_data(real, real + rho_pqrs.size());

  T2 rhopq_tensor = rdm.trace(cc2);
  tmp = rhopq_tensor.trace();
  auto rhopq_trace = std::real(tmp(0));

  xacc::info("Tr(rhopq): " + std::to_string(rhopq_trace));
  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      for (int r = 0; r < nQubits; r++) {
        for (int s = 0; s < nQubits; s++) {
          energy += 0.5 * std::real(hpqrs(p, q, r, s) *
                                    (rdm(p, q, s, r) + rdm(r, s, q, p)));
        }
      }
    }
  }
  //   xacc::info("Energy after 2-rdm: " + std::to_string(energy));

  for (int p = 0; p < nQubits; p++) {
    for (int q = 0; q < nQubits; q++) {
      energy += 0.5 * std::real(hpq(p, q) *
                                (rhopq_tensor(p, q) + rhopq_tensor(q, p)));
    }
  }
  xacc::info("Purified energy " + std::to_string(energy));

  for (auto &b : buffers) {
    b->addExtraInfo("purified-energy", ExtraInfo(energy));
    b->addExtraInfo("non-purified-energy", ExtraInfo(bad_energy));
  }

  buffers[0]->addExtraInfo("noisy-rdm", ExtraInfo(rho_pqrs_data));
  buffers[0]->addExtraInfo("fixed-rdm", ExtraInfo(fixed_rho_pqrs_data));

  xacc::info("Made it here, returning");
  return buffers;
}

} // namespace vqe
} // namespace xacc
