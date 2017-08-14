/***********************************************************************************
 * Copyright (c) 2016, UT-Battelle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the xacc nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contributors:
 *   Initial API and implementation - Alex McCaskey
 *
 **********************************************************************************/
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE DWaveCompilerTester

#include <boost/test/included/unit_test.hpp>
#include "FermionCompiler.hpp"
#include "XACC.hpp"

using namespace xacc::vqe;

class FakeAcc: public xacc::Accelerator {
public:

	virtual std::shared_ptr<xacc::AcceleratorGraph> getAcceleratorConnectivity() {
		return std::make_shared<xacc::AcceleratorGraph>();
	}

	virtual xacc::AcceleratorType getType() {
		return xacc::AcceleratorType::qpu_gate;
	}

	virtual bool isValidBufferSize(const int nBits) {
		return true;
	}

	virtual std::vector<std::shared_ptr<xacc::IRTransformation>> getIRTransformations() {

	}
	;

	virtual std::shared_ptr<xacc::AcceleratorBuffer> createBuffer(
			const std::string& varId) {
		auto b = std::make_shared<xacc::AcceleratorBuffer>(varId, 1);
		storeBuffer(varId, b);
		return b;
	}

	virtual std::vector<std::string> getAllocatedBufferNames() {
		std::vector<std::string> names;
		names.push_back("hello");
		return names;
	}

	virtual std::shared_ptr<xacc::AcceleratorBuffer> getBuffer(
			const std::string& varid) {
		return std::make_shared<xacc::AcceleratorBuffer>("hello", 1);
	}

	virtual void initialize() {

	}
	/**
	 * Execute the provided XACC IR Function on the provided AcceleratorBuffer.
	 *
	 * @param buffer The buffer of bits this Accelerator should operate on.
	 * @param function The kernel to execute.
	 */
	virtual void execute(std::shared_ptr<xacc::AcceleratorBuffer> buffer,
			const std::shared_ptr<xacc::Function> function) {
	}

	virtual const std::string name() const {
		return "fake";
	}

	virtual const std::string description() const {
		return "....";
	}

	/**
	 * Create, store, and return an AcceleratorBuffer with the given
	 * variable id string and of the given number of bits.
	 * The string id serves as a unique identifier
	 * for future lookups and reuse of the AcceleratorBuffer.
	 *
	 * @param varId The variable name of the created buffer
	 * @param size The number of bits in the created buffer
	 * @return buffer The buffer instance created.
	 */
	virtual std::shared_ptr<xacc::AcceleratorBuffer> createBuffer(
			const std::string& varId, const int size) {
		auto b = std::make_shared<xacc::AcceleratorBuffer>(varId, size);
		storeBuffer(varId, b);
		return b;
	}
};

BOOST_AUTO_TEST_CASE(checkSimpleCompile) {

	auto compiler = std::make_shared<FermionCompiler>();

	// 3.17 adag_2 a_0 + 3.17 adag_0 a2
	const std::string simpleFermionHamiltonian = "__qpu__ fermionKernel() {\n"
			"   3.17 2 1 0 0\n"
			"   3.17 0 1 2 0\n"
			"}";

	auto options = xacc::RuntimeOptions::instance();

	auto acc = std::make_shared<FakeAcc>();

	auto ir = compiler->compile(simpleFermionHamiltonian, acc);

	auto expected =
			R"expected({
    "kernels": [
        {
            "function": "term0",
            "instructions": [
                {
                    "gate": "H",
                    "enabled": true,
                    "qubits": [
                        2
                    ]
                },
                {
                    "gate": "Measure",
                    "enabled": true,
                    "qubits": [
                        2
                    ],
                    "classicalBitIdx": 2
                },
                {
                    "gate": "Measure",
                    "enabled": true,
                    "qubits": [
                        1
                    ],
                    "classicalBitIdx": 1
                },
                {
                    "gate": "H",
                    "enabled": true,
                    "qubits": [
                        0
                    ]
                },
                {
                    "gate": "Measure",
                    "enabled": true,
                    "qubits": [
                        0
                    ],
                    "classicalBitIdx": 0
                }
            ]
        },
        {
            "function": "term1",
            "instructions": [
                {
                    "gate": "Rx",
                    "enabled": true,
                    "qubits": [
                        2
                    ],
                    "angle": -1.5707963
                },
                {
                    "gate": "Measure",
                    "enabled": true,
                    "qubits": [
                        2
                    ],
                    "classicalBitIdx": 2
                },
                {
                    "gate": "Measure",
                    "enabled": true,
                    "qubits": [
                        1
                    ],
                    "classicalBitIdx": 1
                },
                {
                    "gate": "Rx",
                    "enabled": true,
                    "qubits": [
                        0
                    ],
                    "angle": -1.5707963
                },
                {
                    "gate": "Measure",
                    "enabled": true,
                    "qubits": [
                        0
                    ],
                    "classicalBitIdx": 0
                }
            ]
        }
    ]
})expected";

	std::stringstream ss;
	ir->persist(ss);
	std::cout << ss.str() << "\n";
	BOOST_VERIFY(expected == ss.str());
}

BOOST_AUTO_TEST_CASE(checkH2MinBondLength) {

	const std::string code = R"code(__qpu__ H2_sto-3g_singlet_H2_Molecule0_71056() {
	0.286177854957 1 1 0 1 0 0 1 0
	0.288294627326 2 1 0 1 0 0 2 0
	-0.962973543364 1 1 1 0
	0.288294627326 3 1 1 1 1 0 3 0
	0.301519238154 3 1 2 1 2 0 3 0
	0.288294627326 1 1 2 1 2 0 1 0
	-0.653040869332 2 1 2 0
	0.109624376716 2 1 1 1 3 0 0 0
	0.288294627326 1 1 3 1 3 0 1 0
	0.286177854957 0 1 1 1 1 0 0 0
	0.109624376716 3 1 2 1 0 0 1 0
	0.301519238154 2 1 3 1 3 0 2 0
	0.109624376716 2 1 0 1 2 0 0 0
	0.109624376716 3 1 0 1 2 0 1 0
	-0.962973543364 0 1 0 0
	0.109624376716 0 1 2 1 0 0 2 0
	0.109624376716 3 1 1 1 3 0 1 0
	-0.653040869332 3 1 3 0
	0.288294627326 3 1 0 1 0 0 3 0
	0.288294627326 0 1 2 1 2 0 0 0
	0.109624376716 0 1 3 1 1 0 2 0
	0.109624376716 1 1 2 1 0 0 3 0
	0.288294627326 2 1 1 1 1 0 2 0
	0.109624376716 0 1 1 1 3 0 2 0
	0.109624376716 1 1 3 1 1 0 3 0
	0.394095527599
	0.109624376716 2 1 3 1 1 0 0 0
	0.109624376716 1 1 0 1 2 0 3 0
	0.288294627326 0 1 3 1 3 0 0 0
})code";

	auto acc = std::make_shared<FakeAcc>();

	auto compiler = std::make_shared<FermionCompiler>();
	auto ir = compiler->compile(code, acc);

//	std::map<std::vector<std::pair<int, std::string>>, std::complex<double>> expected {
//		{{{0,"I"}}, std::complex<double>(-0.461105,0)}
//	};



}
