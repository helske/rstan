#include <gtest/gtest.h>
#include <RInside.h>
#include <rstan/stan_fit.hpp>  
#include "blocker.cpp"
#include <sstream>
#include <fstream>
#include <boost/algorithm/string.hpp>

typedef boost::ecuyer1988 rng_t; // (2**50 = 1T samples, 1000 chains)
typedef stan::mcmc::adapt_diag_e_nuts<stan_model, rng_t> sampler_t;

class RStan : public ::testing::Test {
public:
  RStan() 
    : in_(),
      data_stream_(std::string("tests/cpp/blocker.data.R").c_str()),
      data_context_(data_stream_),
      model_(data_context_, &std::cout),
      sample_stream(),
      diagnostic_stream(),
      base_rng(123U),
      sampler_ptr(new sampler_t(model_, base_rng, 
                                &rstan::io::rcout, &rstan::io::rcerr)) { 
    data_stream_.close();
  }

  ~RStan() {
    delete sampler_ptr;
  }

  static void SetUpTestCase() { 
    RInside R;
  }

  static void TearDownTestCase() { }
  
  void SetUp() {
  }
  
  void TearDown() { 
  }
  

  Rcpp::List in_;
  std::fstream data_stream_;
  stan::io::dump data_context_;
  stan_model model_;
  std::fstream sample_stream;
  std::fstream diagnostic_stream;
  rng_t base_rng;
  sampler_t* sampler_ptr;
};

std::string read_file(const std::string filename) {
  std::ifstream f(filename.c_str());
  if (f) {
    std::string contents;
    f.seekg(0, std::ios::end);
    contents.resize(f.tellg());
    f.seekg(0, std::ios::beg);
    f.read(&contents[0], contents.size());
    f.close();
    return(contents);
  }
  return "";
}

rstan::stan_args stan_args_factory(const std::string& str) {
  Rcpp::List list;
  std::vector<std::string> lines;
  boost::split(lines, str, boost::is_any_of("\n"));
  
  for (size_t n = 0; n < lines.size(); n++) {
    std::vector<std::string> assign;
    boost::split(assign, lines[n], boost::is_any_of("#="));
    if (assign.size() == 3) {
      std::string lhs = assign[1];
      std::string rhs = assign[2];
      boost::trim(lhs);
      boost::trim(rhs);
      
      if (lhs == "sampler_t" || lhs == "init") {
        list[lhs] = rhs;
      } else if (lhs == "seed" || lhs == "chain_id" || lhs == "iter"
                 || lhs == "warmup" || lhs == "save_warmup" || lhs == "thin" 
                 || lhs == "refresh" || lhs == "adapt_engaged" || lhs == "max_treedepth" 
                 || lhs == "append_samples") {
        int int_value;
        std::istringstream(rhs) >> int_value;
        list[lhs] = int_value;
      } else if (lhs == "stepsize" || lhs == "stepsize_jitter" || lhs == "adapt_gamma"
                 || lhs == "adapt_delta" || lhs == "adapt_kappa" 
                 || lhs == "adapt_t0") {
        double double_value;
        std::istringstream(rhs) >> double_value;
        list[lhs] = double_value;
      } else {
        ADD_FAILURE() << "don't know how to handle this line: " << lines[n];
      }
    }
  }
  return rstan::stan_args(list);
}

template <class T>
T convert(const std::string& str) {
  T value;
  std::istringstream(str) >> value;
  return value;
}

template <>
std::string convert(const std::string& str) {
  return str;
}

template <class T>
std::vector<T> vector_factory(const std::string str) {
  std::vector<T> x;
  std::vector<std::string> lines;
  boost::split(lines, str, boost::is_any_of("\n"));
  if (lines.size() == 3) {
    size_t size;
    std::vector<std::string> line;
    boost::split(line, lines[0], boost::is_any_of("()"));
    if (line.size() < 2) {
      ADD_FAILURE() << "can't find the size: " << str;
      return x;
    }
    std::istringstream(line[1]) >> size;
    x.resize(size);
    

    line.clear();
    boost::split(line, lines[1], boost::is_any_of("{},"));
    if (line.size() != size + 2) {
      ADD_FAILURE() << "can't read the correct number of elements: " << str;
      return x;
    }
    
    for (size_t n = 0; n < size; n++) {
      x[n] = convert<T>(line[n+1]);
    }
  } else {
    ADD_FAILURE() << "number of lines (" << lines.size() << ") is not 3: "
                  << str << std::endl;
  }
  return x;
}

void parse_NumericVector(Rcpp::NumericVector& x, const std::string str) { 
  size_t start = str.find("]")+1;
  std::string values = str.substr(start);
  boost::trim(values);
  std::vector<std::string> value;
  boost::split(value, values, boost::is_any_of(" "));
  
  for (size_t m = 0; m < value.size(); m++) {
    double val;
    std::stringstream(value[m]) >> val;
    x.push_back(val);
  }
}

Rcpp::List holder_factory(const std::string str, const rstan::stan_args args) {
  Rcpp::List holder;

  std::vector<std::string> lines;
  boost::split(lines, str, boost::is_any_of("\n"));
  
  for (int n = 0; n < lines.size(); n++) {
    if (lines[n] == "") {
      // no-op: skip line
    } else if (lines[n][0] == '$') {
      std::string name = lines[n].substr(1);
      if (name[0] == '`') {
        name = name.substr(1, name.size()-2);
      }
      n++;
      Rcpp::NumericVector x;
      while (lines[n] != "") {
        parse_NumericVector(x, lines[n]);
        n++;
      }
      holder[name] = x;
    } else if (lines[n].substr(0, 5) == "attr(") {
      std::vector<std::string> tmp;
      boost::split(tmp, lines[n], boost::is_any_of("\""));
      std::string name = tmp[1]; 
      
      if (name == "args" || name == "sampler_params") {
        while (lines[n] != "") 
          n++;
        if (name == "args") {
          holder.attr("args") = args.stan_args_to_rlist();
        } 
        if (name == "sampler_params") {
          Rcpp::List sampler_params;
          sampler_params["accept_stat__"] = double(1);
          sampler_params["stepsize__"] = double(0.0625);
          sampler_params["treedepth__"] = int(1);
          sampler_params["n_leapfrog__"] = int(1);
          sampler_params["n_divergent__"] = int(0);
          holder.attr("sampler_params") = sampler_params;
        }
      } else {
        n++;
        // NumericVector
        if (name == "inits" || name == "mean_pars") {
          Rcpp::NumericVector x;
          while (lines[n].find("]") != std::string::npos) {
            parse_NumericVector(x, lines[n]);
            n++;
          }
          n--;
          holder.attr(name) = x;
        } else if (name == "test_grad") {  // booleans
          bool x;          
          size_t start = lines[n].find("]")+1;
          std::string value = lines[n].substr(start);
          boost::trim(value);
          std::stringstream(value) >> x;
          
          holder.attr(name) = value;
        } else if (name == "mean_lp__") { // doubles
          double x;
          size_t start = lines[n].find("]")+1;
          std::string value = lines[n].substr(start);
          boost::trim(value);
          std::stringstream(value) >> x;
          
          holder.attr(name) = x;
        } else if (name == "adaptation_info") { // strings
          size_t start = lines[n].find("]")+1;
          std::string value = lines[n].substr(start);
          boost::trim(value);
          value = value.substr(1, value.size() - 2);
          holder.attr(name) = value;
        } else {
          std::cout << "attr line: " << std::endl;
          std::cout << name << " = ";
          std::cout << "  full line: " << lines[n] << std::endl;
        }
      }
    } 
  }
  return holder;
}

void test_holder(const Rcpp::List e, const Rcpp::List x) { 
  {
    ASSERT_EQ(e.size(), x.size());
    for (size_t n = 0; n < e.size(); n++) {
      Rcpp::NumericVector e_vec = e[n];
      Rcpp::NumericVector x_vec = x[n];
      for (size_t i = 0; i < e_vec.size(); i++) {
        EXPECT_FLOAT_EQ(e_vec[i], x_vec[i]) 
          << "the " << n << "th variable, " << i << "th variable is off";
      }
    }
  }
  {
    Rcpp::NumericVector e_inits = e.attr("inits");
    Rcpp::NumericVector x_inits = x.attr("inits");
    
    ASSERT_EQ(e_inits.size(), x_inits.size());
    for (size_t n = 0; n < e_inits.size(); n++)
      EXPECT_FLOAT_EQ(e_inits[n], x_inits[n]);
  }
  {
    Rcpp::NumericVector e_mean_pars = e.attr("mean_pars");
    Rcpp::NumericVector x_mean_pars = x.attr("mean_pars");
    
    ASSERT_EQ(e_mean_pars.size(), x_mean_pars.size());
    for (size_t n = 0; n < e_mean_pars.size(); n++)
      EXPECT_FLOAT_EQ(e_mean_pars[n], x_mean_pars[n]);
  }
  {
    double e_mean_lp__ = e.attr("mean_lp__");
    double x_mean_lp__ = x.attr("mean_lp__");
    
    EXPECT_FLOAT_EQ(e_mean_lp__, x_mean_lp__);
  }

}

TEST_F(RStan, execute_sampling_1) {

  std::stringstream ss;

  std::string e_args_string = read_file("tests/cpp/test_config/1_input_stan_args.txt");
  rstan::stan_args args = stan_args_factory(e_args_string);
  args.write_args_as_comment(ss);
  ASSERT_EQ(e_args_string, ss.str());
  ss.str("");
  
  stan::mcmc::sample s(Eigen::VectorXd::Zero(model_.num_params_r()), 0, 0);
  
  std::vector<size_t> qoi_idx 
    = vector_factory<size_t>(read_file("tests/cpp/test_config/1_input_qoi_idx.txt"));
  std::vector<double> initv 
    = vector_factory<double>(read_file("tests/cpp/test_config/1_input_initv.txt"));
  std::vector<std::string> fnames_oi 
    = vector_factory<std::string>(read_file("tests/cpp/test_config/1_input_fnames_oi.txt"));
  
  std::string e_holder_string = read_file("tests/cpp/test_config/1_output_holder.txt");
  Rcpp::List e_holder 
    = holder_factory(read_file("tests/cpp/test_config/1_output_holder.txt"),
                     args);

  Rcpp::List holder;
  
  rstan::init_nuts<sampler_t>(sampler_ptr, args);
  Eigen::VectorXd tmp = Eigen::VectorXd::Zero(model_.num_params_r());
  rstan::init_windowed_adapt<sampler_t>(sampler_ptr, args, s.cont_params());
  
  
  std::fstream sample_stream, diagnostic_stream;
  rstan::execute_sampling(args, model_, holder,
                          sampler_ptr,
                          s,
                          qoi_idx,
                          initv,
                          sample_stream,
                          diagnostic_stream,
                          fnames_oi,
                          base_rng);
  
  test_holder(e_holder, holder);
}
