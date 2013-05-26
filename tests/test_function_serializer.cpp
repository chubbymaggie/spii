// Petter Strandmark 2013.
#include <fstream>
#include <random>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include <spii/auto_diff_term.h>
#include <spii/function_serializer.h>
#include <spii/solver.h>

using namespace std;
using namespace spii;

int global_number_of_terms = 0;

template<int dim>
struct Norm
{
	Norm()
	{
		global_number_of_terms++;
	}

	~Norm()
	{
		global_number_of_terms--;
	}

	template<typename R>
	R operator()(const R* const x) const
	{
		R d = 0;
		for (int i = 0; i < dim; ++i) {
			d +=  x[i] * x[i];
		}

		using std::sqrt;
		return sqrt(d);
	}
};

template<int dim1, int dim2>
struct NormTwo
{
	NormTwo()
	{
		global_number_of_terms++;
	}

	~NormTwo()
	{
		global_number_of_terms--;
	}

	template<typename R>
	R operator()(const R* const x, const R* const y) const
	{
		R d = 0;
		for (int i = 0; i < dim1; ++i) {
			d +=  3.0 * x[i] * x[i];
		}

		for (int i = 0; i < dim2; ++i) {
			d +=  5.0 * y[i] * y[i];
		}

		using std::sqrt;
		return sqrt(d);
	}
};

TEST_CASE("Serialize/write_read", "")
{
	auto tmp = "tmp";
	double f_value = 0;

	// First, create a function and write it to a
	// temporary file.
	{
		Function f;
		double x1[1] = {10.0};
		double x2[3] = {20.0, 30.0, 40.0};
		f.add_variable(x1, 1);
		f.add_variable(x2, 3);
		f.add_term(new AutoDiffTerm<Norm<1>, 1>(new Norm<1>), x1);
		f.add_term(new AutoDiffTerm<Norm<3>, 3>(new Norm<3>), x2);
		f.add_term(new AutoDiffTerm<NormTwo<3,1>, 3, 1>(new NormTwo<3, 1>), x2, x1);

		ofstream fout(tmp);
		fout << Serialize(f);
		fout.close();

		f_value = f.evaluate();
		CHECK(global_number_of_terms == 3);
	}

	CHECK(global_number_of_terms == 0);

	// Second, read the file and check that the function
	// evaluates to the same values.
	{
		TermFactory factory;
		factory.teach_term<AutoDiffTerm<Norm<1>, 1>>();
		factory.teach_term<AutoDiffTerm<Norm<3>, 3>>();
		factory.teach_term<AutoDiffTerm<NormTwo<3,1>, 3, 1>>();

		Function f2;
		std::vector<double> x;
		ifstream fin(tmp);
		fin >> Serialize(&f2, &x, factory);
		fin.close();

		CHECK(f_value == f2.evaluate());
		CHECK(global_number_of_terms == 3);
	}

	CHECK(global_number_of_terms == 0);
}

struct Rosenbrock
{
	template<typename R>
	R operator()(const R* const x) const
	{
		R d0 =  x[1] - x[0]*x[0];
		R d1 =  1 - x[0];
		return 100 * d0*d0 + d1*d1;
	}
};

TEST_CASE("Serialize/minimization", "")
{
	const auto tmp = "tmp";
	const int num_start_values = 10;

	vector<pair<double, double>> start_values;
	for (int i = 0; i < num_start_values; ++i) {
		start_values.emplace_back(5 - i, i);
	}
	vector<double> results(num_start_values);

	// First, create a function and write it to a
	// temporary file.
	{
		Function f;
		double x[2] = {0};
		f.add_variable(x, 2);
		f.add_term(new AutoDiffTerm<Rosenbrock, 2>(new Rosenbrock), x);

		ofstream fout(tmp);
		fout << Serialize(f);
		fout.close();

		Solver solver;
		solver.log_function = [](const string&) { };
		for (int i = 0; i < num_start_values; ++i) {
			SolverResults r;
			x[0] = start_values[i].first;
			x[1] = start_values[i].second;
			solver.solve_newton(f, &r);
			results[i] = f.evaluate();
		}
	}

	// Second, read the file and check that the function
	// evaluates to the same values.
	{
		TermFactory factory;
		factory.teach_term<AutoDiffTerm<Rosenbrock, 2>>();

		Function f2;
		std::vector<double> x;
		ifstream fin(tmp);
		fin >> Serialize(&f2, &x, factory);
		fin.close();

		Solver solver;
		solver.log_function = [](const string&) { };
		for (int i = 0; i < num_start_values; ++i) {
			SolverResults r;
			x[0] = start_values[i].first;
			x[1] = start_values[i].second;
			solver.solve_newton(f2, &r);
			CHECK(results[i] == f2.evaluate());
		}
	}
}

// One term in the negative log-likelihood function for
// a one-dimensional Gaussian distribution.
struct NegLogLikelihood
{
	double sample;
	NegLogLikelihood(double sample)
	{
		this->sample = sample;
	}
	NegLogLikelihood()
	{
		this->sample = 0;
	}

	template<typename R>
	R operator()(const R* const mu, const R* const sigma) const
	{
		R diff = (*mu - sample) / *sigma;
		return 0.5 * diff*diff + log(*sigma);
	}

	void read(std::istream& in)
	{
		in >> sample;
	}

	void write(std::ostream& out) const
	{
		out << sample;
	}
};

TEST_CASE("Serialize/FunctorData", "")
{
	const auto tmp = "tmp";

	mt19937 prng(0);
	normal_distribution<double> normal;
	auto randn = bind(normal, prng);

	double mu_result, sigma_result;
	Solver solver;
	solver.log_function = [](const string&) { };

	{
		double mu    = 5.0;
		double sigma = 3.0;

		Function f;
		f.add_variable(&mu, 1);
		f.add_variable(&sigma, 1);

		for (int i = 0; i < 100; ++i) {
			double sample = sigma*randn() + mu;
			auto* llh = new NegLogLikelihood(sample);
			f.add_term(new AutoDiffTerm<NegLogLikelihood, 1, 1>(llh), &mu, &sigma);
		}

		ofstream fout(tmp);
		fout << Serialize(f);

		
		mu    = 0.0;
		sigma = 1.0;
		SolverResults results;
		solver.solve_lbfgs(f, &results);

		mu_result = mu;
		sigma_result = sigma;
	}

	{
		TermFactory factory;
		factory.teach_term<AutoDiffTerm<NegLogLikelihood, 1, 1>>();

		ifstream fin(tmp);
		Function f;
		vector<double> user_space;
		fin >> Serialize(&f, &user_space, factory);

		SolverResults results;
		user_space[0] = 0.0;
		user_space[1] = 1.0;
		solver.solve_lbfgs(f, &results);

		CHECK(user_space[0] == mu_result);
		CHECK(user_space[1] == sigma_result);
	}
}
