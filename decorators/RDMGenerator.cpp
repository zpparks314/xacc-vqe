#include "RDMGenerator.hpp"
#include "XACC.hpp"
#include <unsupported/Eigen/CXX11/TensorSymmetry>

namespace xacc {
namespace vqe {

void RDMGenerator::generate(std::shared_ptr<Function> ansatz) {
  // Reset
  rho_pq.setZero();
  rho_pqrs.setZero();
  // Get the Accelerator we're running on, the
  // number of orbitals/qubits in the problem,
  // the MPIProvider Communicator reference, and the
  // VQE state preparation ansatz.
  int nQubits = _nQubits, nExecs = 0;

  // Silence the FermionCompiler info messages.
  xacc::setOption("fermion-compiler-silent", "");

  // Get the FermionCompiler, we'll use it to map our
  // RDM second quantized expressions to Paulis that
  // can be measured on the QPU.
  auto fermionCompiler = xacc::getCompiler("fermion");

  Eigen::DynamicSGroup rho_pq_Sym, rho_pqrs_Sym;
  rho_pq_Sym.addHermiticity(0, 1);
  rho_pqrs_Sym.addAntiSymmetry(0, 1);
  rho_pqrs_Sym.addAntiSymmetry(2, 3);

  std::vector<std::shared_ptr<Function>> nontrivial_functions;
  std::vector<double> coefficients;
  std::vector<std::vector<int>> rho_elements;
  std::map<std::vector<int>, double> rho_element_2_identity_coeff;

  // Generate the 2-RDM circuits for executiong
  for (int m = 0; m < nQubits; m++) {
    for (int n = m + 1; n < nQubits; n++) {
      for (int v = m; v < nQubits; v++) {
        for (int w = v + 1; w < nQubits; w++) {
          // Create the source code XACC kernel and compile it
          std::stringstream xx;
          xx << "__qpu__ k(){\n0.5 " << m << " 1 " << n << " 1 " << w << " 0 "
             << v << " 0\n"
             << "0.5 " << w << " 1 " << v << " 1 " << m << " 0 " << n
             << " 0\n}";
          auto hpqrs_ir = fermionCompiler->compile(xx.str(), qpu);

          // Loop over the kernels, execute them and compute
          // the weight * expVal sum
          double sum_pqrs = 0.0;
          for (auto &kernel : hpqrs_ir->getKernels()) {

            double localExpVal = 1.0;
            auto t = std::real(
                boost::get<std::complex<double>>(kernel->getParameter(0)));
            if (kernel->nInstructions() > 0) {
              kernel->insertInstruction(0, ansatz);
              nontrivial_functions.push_back(kernel);
              coefficients.push_back(t);
              rho_elements.push_back({m, n, v, w});
              //   xacc::info(kernel->toString() + "\n");
            } else {
              rho_element_2_identity_coeff.insert({{m, n, v, w}, t});
            }
          }
        }
      }
    }
  }

  // Execute all nontrivial circuits
  xacc::info("Executing " + std::to_string(nontrivial_functions.size()) +
             " circuits to compute rho_pqrs.");
  auto buffer = qpu->createBuffer("q", nQubits);
  auto buffers = qpu->execute(buffer, nontrivial_functions);

  // Create a mapping of rho_pqrs elements to summed expectation values for
  // each circuit contributing to it
  std::map<std::vector<int>, double> sumMap;
  for (int i = 0; i < buffers.size(); i++) {
    auto elements = rho_elements[i];
    auto value = coefficients[i] * buffers[i]->getExpectationValueZ();
    if (!sumMap.count(elements)) {
      sumMap.insert({elements, value});
    } else {
      sumMap[elements] += value;
    }
  }

  // Add all identity terms, we don't execute them
  // but we still have to add their contribution
  for (auto &kv : rho_element_2_identity_coeff) {
    sumMap[kv.first] += kv.second;
  }

  // Set rho_pqrs. This is all we need
  // to get rho_pq as well
  for (auto &kv : sumMap) {
    auto elements = kv.first;
    rho_pqrs_Sym(rho_pqrs, elements[0], elements[1], elements[2], elements[3]) =
        kv.second;
    rho_pqrs_Sym(rho_pqrs, elements[2], elements[3], elements[0], elements[1]) =
        kv.second;
  }
}

const double RDMGenerator::energy() { return _energy; }

} // namespace vqe
} // namespace xacc