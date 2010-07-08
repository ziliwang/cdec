//TODO: 0 size state != rule-local feature, i.e. still may depend on source span loc/context.  identify truly rule-local features so if we want they can be added to grammar rules (minor speedup)

#include "ff.h"

#include "tdict.h"
#include "hg.h"

using namespace std;

FeatureFunction::~FeatureFunction() {}


void FeatureFunction::FinalTraversalFeatures(const void* ant_state,
                                             SparseVector<double>* features) const {
  (void) ant_state;
  (void) features;
}

string FeatureFunction::usage_helper(std::string const& name,std::string const& params,std::string const& details,bool sp,bool sd) {
  string r=name;
  if (sp) {
    r+=": ";
    r+=params;
  }
  if (sd) {
    r+="\n";
    r+=details;
  }
  return r;
}

FeatureFunction::Features FeatureFunction::single_feature(WordID feat) {
  return Features(1,feat);
}

FeatureFunction::Features ModelSet::all_features(std::ostream *warn) {
  typedef FeatureFunction::Features FFS;
  FFS ffs;
#define WARNFF(x) do { if (warn) { *warn << "WARNING: "<< x ; *warn<<endl; } } while(0)
  typedef std::map<WordID,string> FFM;
  FFM ff_from;
  for (unsigned i=0;i<models_.size();++i) {
    FeatureFunction const& ff=*models_[i];
    string const& ffname=ff.name;
    FFS si=ff.features();
    if (si.empty()) {
      WARNFF(ffname<<" doesn't yet report any feature IDs - implement features() method?");
    }
    for (unsigned j=0;j<si.size();++j) {
      WordID fid=si[j];
      if (fid >= weights_.size())
        weights_.resize(fid+1);
      pair<FFM::iterator,bool> i_new=ff_from.insert(FFM::value_type(fid,ffname));
      if (i_new.second)
        ffs.push_back(fid);
      else {
        WARNFF(ffname<<" models["<<i<<"] tried to define feature "<<FD::Convert(fid)<<" already defined earlier by "<<i_new.first->second);
      }
    }
  }
  return ffs;
#undef WARNFF
}

void ModelSet::show_features(std::ostream &out,std::ostream &warn,bool warn_zero_wt)
{
  typedef FeatureFunction::Features FFS;
  FFS ffs=all_features(&warn);
  out << "Weight  Feature\n";
  for (unsigned i=0;i<ffs.size();++i) {
    WordID fid=ffs[i];
    string const& fname=FD::Convert(fid);
    double wt=weights_[fid];
    if (warn_zero_wt && wt==0)
      warn<<"WARNING: "<<fname<<" has 0 weight."<<endl;
    out << wt << "  " << fname<<endl;
  }

}

// Hiero and Joshua use log_10(e) as the value, so I do to
WordPenalty::WordPenalty(const string& param) :
    fid_(FD::Convert("WordPenalty")),
    value_(-1.0 / log(10)) {
  if (!param.empty()) {
    cerr << "Warning WordPenalty ignoring parameter: " << param << endl;
  }
}

void WordPenalty::TraversalFeaturesImpl(const SentenceMetadata& smeta,
                                        const Hypergraph::Edge& edge,
                                        const std::vector<const void*>& ant_states,
                                        SparseVector<double>* features,
                                        SparseVector<double>* estimated_features,
                                        void* state) const {
  (void) smeta;
  (void) ant_states;
  (void) state;
  (void) estimated_features;
  features->set_value(fid_, edge.rule_->EWords() * value_);
}

SourceWordPenalty::SourceWordPenalty(const string& param) :
    fid_(FD::Convert("SourceWordPenalty")),
    value_(-1.0 / log(10)) {
  if (!param.empty()) {
    cerr << "Warning SourceWordPenalty ignoring parameter: " << param << endl;
  }
}

FeatureFunction::Features SourceWordPenalty::features() const {
  return single_feature(fid_);
}

FeatureFunction::Features WordPenalty::features() const {
  return single_feature(fid_);
}


void SourceWordPenalty::TraversalFeaturesImpl(const SentenceMetadata& smeta,
                                        const Hypergraph::Edge& edge,
                                        const std::vector<const void*>& ant_states,
                                        SparseVector<double>* features,
                                        SparseVector<double>* estimated_features,
                                        void* state) const {
  (void) smeta;
  (void) ant_states;
  (void) state;
  (void) estimated_features;
  features->set_value(fid_, edge.rule_->FWords() * value_);
}

ArityPenalty::ArityPenalty(const std::string& /* param */) :
    value_(-1.0 / log(10)) {
  string fname = "Arity_X";
  for (int i = 0; i < N_ARITIES; ++i) {
    fname[6]=i + '0';
    fids_[i] = FD::Convert(fname);
  }
}

FeatureFunction::Features ArityPenalty::features() const {
  return Features(&fids_[0],&fids_[N_ARITIES]);
}

void ArityPenalty::TraversalFeaturesImpl(const SentenceMetadata& smeta,
                                         const Hypergraph::Edge& edge,
                                         const std::vector<const void*>& ant_states,
                                         SparseVector<double>* features,
                                         SparseVector<double>* estimated_features,
                                         void* state) const {
  (void) smeta;
  (void) ant_states;
  (void) state;
  (void) estimated_features;
  features->set_value(fids_[edge.Arity()], value_);
}

ModelSet::ModelSet(const vector<double>& w, const vector<const FeatureFunction*>& models) :
    models_(models),
    weights_(w),
    state_size_(0),
    model_state_pos_(models.size()) {
  for (int i = 0; i < models_.size(); ++i) {
    model_state_pos_[i] = state_size_;
    state_size_ += models_[i]->NumBytesContext();
  }
}

void ModelSet::AddFeaturesToEdge(const SentenceMetadata& smeta,
                                 const Hypergraph& /* hg */,
                                 const vector<string>& node_states,
                                 Hypergraph::Edge* edge,
                                 string* context,
                                 prob_t* combination_cost_estimate) const {
  context->resize(state_size_);
  memset(&(*context)[0], 0, state_size_);
  SparseVector<double> est_vals;  // only computed if combination_cost_estimate is non-NULL
  if (combination_cost_estimate) *combination_cost_estimate = prob_t::One();
  for (int i = 0; i < models_.size(); ++i) {
    const FeatureFunction& ff = *models_[i];
    void* cur_ff_context = NULL;
    vector<const void*> ants(edge->tail_nodes_.size());
    bool has_context = ff.NumBytesContext() > 0;
    if (has_context) {
      int spos = model_state_pos_[i];
      cur_ff_context = &(*context)[spos];
      for (int i = 0; i < ants.size(); ++i) {
        ants[i] = &node_states[edge->tail_nodes_[i]][spos];
      }
    }
    ff.TraversalFeatures(smeta, *edge, ants, &edge->feature_values_, &est_vals, cur_ff_context);
  }
  if (combination_cost_estimate)
    combination_cost_estimate->logeq(est_vals.dot(weights_));
  edge->edge_prob_.logeq(edge->feature_values_.dot(weights_));
}

void ModelSet::AddFinalFeatures(const std::string& state, Hypergraph::Edge* edge) const {
  assert(1 == edge->rule_->Arity());

  for (int i = 0; i < models_.size(); ++i) {
    const FeatureFunction& ff = *models_[i];
    const void* ant_state = NULL;
    bool has_context = ff.NumBytesContext() > 0;
    if (has_context) {
      int spos = model_state_pos_[i];
      ant_state = &state[spos];
    }
    ff.FinalTraversalFeatures(ant_state, &edge->feature_values_);
  }
  edge->edge_prob_.logeq(edge->feature_values_.dot(weights_));
}

