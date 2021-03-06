// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "sample_pdf.h"
#include "err_constants.h"
#include "api_status.h"
#include "debug_log.h"
#include "parse_args.h"
#include "explore.h"
#include "guard.h"

// Aliases
using std::endl;
using VW::cb_continuous::continuous_label;
using VW::cb_continuous::continuous_label_elm;
using VW::config::make_option;
using VW::config::option_group_definition;
using VW::config::options_i;
using VW::LEARNER::single_learner;

namespace VW
{
namespace continuous_action
{
////////////////////////////////////////////////////
// BEGIN sample_pdf reduction and reduction methods
struct sample_pdf
{
  int learn(example& ec, experimental::api_status* status);
  int predict(example& ec, experimental::api_status* status);

  void init(single_learner* p_base, uint64_t* p_random_seed);
  ~sample_pdf();

private:
  uint64_t* _p_random_state;
  continuous_actions::probability_density_function _pred_pdf;
  single_learner* _base = nullptr;
};

int sample_pdf::learn(example& ec, experimental::api_status*)
{
  // one of the base reductions will call predict so we need a valid
  // predict buffer
  _pred_pdf.clear();
  {  // scope to predict & restore prediction
    auto restore = VW::swap_guard(ec.pred.pdf, _pred_pdf);
    _base->learn(ec);
  }
  return error_code::success;
}

int sample_pdf::predict(example& ec, experimental::api_status*)
{
  _pred_pdf.clear();

  {  // scope to predict & restore prediction
    auto restore = VW::swap_guard(ec.pred.pdf, _pred_pdf);
    _base->predict(ec);
  }

  const int ret_code = exploration::sample_pdf(_p_random_state, std::begin(_pred_pdf), std::end(_pred_pdf),
      ec.pred.pdf_value.action, ec.pred.pdf_value.pdf_value);

  if (ret_code != S_EXPLORATION_OK) return error_code::sample_pdf_failed;

  return error_code::success;
}

void sample_pdf::init(single_learner* p_base, uint64_t* p_random_seed)
{
  _base = p_base;
  _p_random_state = p_random_seed;
  _pred_pdf = v_init<continuous_actions::pdf_segment>();
}

sample_pdf::~sample_pdf() { _pred_pdf.delete_v(); }

// Free function to tie function pointers to reduction class methods
template <bool is_learn>
void predict_or_learn(sample_pdf& reduction, single_learner&, example& ec)
{
  experimental::api_status status;
  if (is_learn)
    reduction.learn(ec, &status);
  else
  {
    if (error_code::success != reduction.predict(ec, &status)) THROW(error_code::sample_pdf_failed_s);
  }

  if (status.get_error_code() != error_code::success) { VW_DBG(ec) << status.get_error_msg() << endl; }
}

// END sample_pdf reduction and reduction methods
////////////////////////////////////////////////////

LEARNER::base_learner* sample_pdf_setup(options_i& options, vw& all)
{
  option_group_definition new_options("Continuous actions - sample pdf");
  bool invoked = false;
  new_options.add(
      make_option("sample_pdf", invoked).keep().necessary().help("Sample a pdf and pick a continuous valued action"));

  // If sample_pdf reduction was not invoked, don't add anything
  // to the reduction stack;
  if (!options.add_parse_and_check_necessary(new_options)) return nullptr;

  LEARNER::base_learner* p_base = setup_base(options, all);
  auto p_reduction = scoped_calloc_or_throw<sample_pdf>();
  p_reduction->init(as_singleline(p_base), &all.random_seed);

  LEARNER::learner<sample_pdf, example>& l = init_learner(p_reduction, as_singleline(p_base), predict_or_learn<true>,
      predict_or_learn<false>, 1, prediction_type_t::action_pdf_value, all.get_setupfn_name(sample_pdf_setup));

  return make_base(l);
}
}  // namespace continuous_action
}  // namespace VW
