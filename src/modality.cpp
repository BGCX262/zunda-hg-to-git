#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <boost/unordered_map.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>
#include <cabocha.h>
#include <iomanip>

#include "../tinyxml2/tinyxml2.h"
#include "sentence.hpp"
#include "modality.hpp"
#include "cdbmap.hpp"

namespace modality {
	std::string parser::id2tag(unsigned int id) {
		switch(id) {
			case SOURCE:
				return "source";
			case TENSE:
				return "tense";
			case ASSUMPTIONAL:
				return "assumptional";
			case TYPE:
				return "type";
			case AUTHENTICITY:
				return "authenticity";
			case SENTIMENT:
				return "sentiment";
			default:
				return NULL;
		}
	}


	void parser::set_model_dir(std::string dir) {
		boost::filesystem::path dir_path(dir);
		set_model_dir(dir_path);
	}


	void parser::set_model_dir(boost::filesystem::path dir_path) {
		for (unsigned int i=0 ; i<LABEL_NUM ; ++i) {
			boost::filesystem::path mp("model_" + id2tag(i));
			model_path[i] = dir_path / mp;

			boost::filesystem::path fp("feat_" + id2tag(i));
			feat_path[i] = dir_path / fp;
		}
		
		boost::filesystem::path l2ip("label2id.cdb");
		l2i_path = dir_path / l2ip;
		boost::filesystem::path l2idp("label2id.cdb.dump");
		l2id_path = dir_path / l2idp;
		
		boost::filesystem::path i2lp("id2label.cdb");
		i2l_path = dir_path / i2lp;
		boost::filesystem::path i2ldp("id2label.cdb.dump");
		i2ld_path = dir_path / i2ldp;

		boost::filesystem::path f2ip("feat2id.cdb");
		f2i_path = dir_path / f2ip;
		boost::filesystem::path f2idp("feat2id.cdb.dump");
		f2id_path = dir_path / f2idp;

		open_f2i_cdb();
		open_l2i_cdb();
		open_i2l_cdb();
	}


	bool parser::detect_target(nlp::token tok) {
		std::string pos_set = tok.pos + "\t" + tok.pos1;
		switch (target_detection) {
			case DETECT_BY_POS:
				if (std::find(target_pos.begin(), target_pos.end(), pos_set) != target_pos.end()) {
					return true;
				}
				break;
			case DETECT_BY_PAS:
				if (tok.pas.is_pred()) {
					return true;
				}
				break;
			case DETECT_BY_ML:
				std::cerr << "SORRY: this method has not been implemented" << std::endl;
				return false;
			case DETECT_BY_GOLD:
				if (tok.has_mod) {
					return true;
				}
				break;
			default:
				std::cerr << "ERROR: no such detection method " << target_detection << std::endl;
				return false;
		}
		return false;
	}


	bool parser::load_models(boost::filesystem::path *model_path_new) {
		model_path = model_path_new;
		return load_models();
	}


	bool parser::load_models() {
		boost::system::error_code error;
		BOOST_FOREACH (unsigned int i, analyze_tags) {
			const bool res = boost::filesystem::exists(model_path[i], error);
			if (!res || error) {
				std::cerr << "ERROR: " << model_path[i].string() << " not found" << std::endl;
				return false;
			}
			else {
#ifdef _MODEBUG
				std::cerr << "loaded " << model_path[i].string() << ": " << boost::filesystem::file_size(model_path[i]) << " byte, " << boost::posix_time::from_time_t(boost::filesystem::last_write_time(model_path[i])) << std::endl;
#endif
				models[i] = linear::load_model( model_path[i].string().c_str() );
			}
		}

		return true;
	}


	nlp::sentence parser::analyze(std::string text, int input_layer) {
		nlp::sentence sent;
		sent.ma_tool = nlp::sentence::MeCab;
		std::string parsed_text;
		switch (input_layer) {
			case RAW:
				parsed_text = cabocha->parseToString( text.c_str() );
				sent.parse_cabocha(parsed_text);
				break;
			case CABOCHA:
			case SYNCHA:
				parsed_text = text;
				sent.parse_cabocha(parsed_text);
				break;
			case KNP_DEP:
			case KNP_PAS:
				parsed_text = text;
				sent.parse_knp(parsed_text);
				break;
			default:
				std::cerr << "invalid input layer" << std::endl;
				break;
		}

		nlp::sentence parsed_sent = analyze(sent);

		return parsed_sent;
	}


	bool comp_xx(const linear::feature_node &xx1, const linear::feature_node &xx2) {
		return xx1.index < xx2.index;
	}

	linear::feature_node* parser::pack_feat_linear(t_feat feat) {
		linear::feature_node* xx = new linear::feature_node[feat.size()+1];
		t_feat::iterator it_feat;
		int feat_cnt = 0;
		for (it_feat=feat.begin() ; it_feat!=feat.end() ; ++it_feat) {
			int feat_id;
			//std::cerr << it_feat->first;
			if (f2i.get(it_feat->first, &feat_id)) {
				//std::cerr << "\t" << feat_id << std::endl;
				xx[feat_cnt].index = feat_id;
				xx[feat_cnt].value = it_feat->second;
				feat_cnt++;
			}
			else {
				//std::cerr << " not found" << std::endl;
			}
		}
		// sorted by feature ID
		// not run sort() when xx is empty
		if (feat_cnt > 0) {
			std::sort(xx, xx+feat_cnt-1, comp_xx);
		}
		xx[feat_cnt].index = -1;
		
		return xx;
	}


	std::string parser::analyzeToString(std::string text, int input_layer) {
		nlp::sentence parsed_sent = analyze(text, input_layer);
		std::stringstream cabocha_ss;

		int eve_id = 0;
		BOOST_FOREACH ( nlp::chunk chk, parsed_sent.chunks ) {
			BOOST_FOREACH ( nlp::token tok, chk.tokens) {
				if (tok.has_mod) {
					cabocha_ss << "#EVENT" << eve_id << "\t" << tok.mod.str() << "\n";
					eve_id++;
				}
			}
		}
		
		cabocha_ss << parsed_sent.input_orig;
		
		return cabocha_ss.str();
	}


	std::string parser::analyzeToString(nlp::sentence sent) {
		nlp::sentence parsed_sent = analyze(sent);
		std::stringstream cabocha_ss;

		int eve_id = 0;
		BOOST_FOREACH ( nlp::chunk chk, parsed_sent.chunks ) {
			BOOST_FOREACH ( nlp::token tok, chk.tokens) {
				if (tok.has_mod) {
					cabocha_ss << "#EVENT" << eve_id << "\t" << tok.mod.str() << "\n";
					eve_id++;
				}
			}
		}
		
		cabocha_ss << parsed_sent.input_orig;
		
		return cabocha_ss.str();
	}


	nlp::sentence parser::analyze(nlp::sentence sent) {
		std::vector<nlp::chunk>::reverse_iterator rit_chk;
		std::vector<nlp::token>::reverse_iterator rit_tok;
		for (rit_chk=sent.chunks.rbegin() ; rit_chk!=sent.chunks.rend() ; ++rit_chk) {
			for (rit_tok=(rit_chk->tokens).rbegin() ; rit_tok!=(rit_chk->tokens).rend() ; ++rit_tok) {
				if (detect_target(*rit_tok)) {
#ifdef _MODEBUG
					std::cerr << "* " << rit_tok->orig << " - " << rit_chk->str() << std::endl;
#endif
					rit_tok->mod.tids.push_back(rit_tok->id);
					rit_tok->has_mod = true;
					rit_chk->has_mod = true;

					feature_generator fgen(sent, rit_tok->id);
					fgen.gen_feature_basic(3);
					fgen.gen_feature_function();
					fgen.gen_feature_dst_chunks();
					fgen.gen_feature_ttj(&dbr_ttj);

					t_feat::iterator it_feat;

					BOOST_FOREACH (unsigned int i, analyze_tags) {
						fgen.update(sent);
						switch (i) {
							case TENSE:
								break;
							case ASSUMPTIONAL:
								break;
							case TYPE:
								fgen.gen_feature_mod("tense");
								fgen.gen_feature_fadic(&dbr_fadic);
								break;
							case AUTHENTICITY:
								fgen.gen_feature_mod("type");
								fgen.gen_feature_fadic(&dbr_fadic);
								break;
							case SENTIMENT:
								fgen.gen_feature_fadic(&dbr_fadic);
								break;
						}

						t_feat compiled_feat = fgen.compile_feat(use_feats[i]);
						linear::feature_node* xx = pack_feat_linear(compiled_feat);

					
						int predicted = linear::predict(models[i], xx);
						std::string label;
						if (i2l.get(predicted, &label)) {
							rit_tok->mod.tag[id2tag(i)] = label;
						}
						else {
							std::cerr << "ERORR: unknown predicted label: " << predicted << std::endl;
							exit(-1);
						}

#ifdef _MODEBUG
						std::string feat_str = fgen.compile_feat_str(use_feats[i]);
						std::cerr << " " << id2tag(i) << ": " << feat_str << " -> " << label << "(" << predicted << ")" << std::endl;
#endif
					}
				}
			}
		}
					
/*
					t_feat *feat;
					feat = new t_feat;
					gen_feature( sent, rit_tok->id, *feat );
					t_feat::iterator it_feat;

					linear::feature_node* xx = pack_feat_linear(feat, read_only);

#ifdef _MODEBUG
					std::string feat_str = "";
					for (it_feat=feat->begin() ; it_feat!=feat->end() ; ++it_feat) {
						std::stringstream ss;
						ss << it_feat->first;
						ss << ":";
						ss << it_feat->second;
						ss << " ";
						feat_str += ss.str();
					}

					std::cout << feat_str << std::endl;
#endif

#ifdef _MODEBUG
					std::cout << " ->";
#endif
					std::string labels[LABEL_NUM];
					BOOST_FOREACH (unsigned int i, analyze_tags) {
						int predicted = linear::predict(models[i], xx);
						boost::unordered_map< std::string, int >::iterator it;
						for (it=label2id.begin() ; it!=label2id.end() ; ++it) {
							if (predicted == it->second) {
								labels[i] = it->first;
								rit_tok->mod.tag[id2tag(i)] = labels[i];
								break;
							}
						}
						if (labels[i] == "") {
							std::cerr << "ERORR: unknown predicted label: " << predicted << std::endl;
							exit(-1);
						}
#ifdef _MODEBUG
						std::cout << " " << labels[i] << "(" << predicted << ")";
#endif
					}
#ifdef _MODEBUG
					std::cout << std::endl;
#endif

					BOOST_FOREACH (unsigned int i, analyze_tags) {
						rit_tok->mod.tag[id2tag(i)] = labels[i];
				}
			}
		}
		*/
		return sent;
	}


	void parser::load_deppasmods(std::vector< std::string > deppasmods, int input_format) {
		learning_data.clear();
		
		BOOST_FOREACH ( std::string deppasmod, deppasmods ) {
			std::cout << deppasmod << std::endl;
			boost::filesystem::path p(deppasmod);
			std::string sent_id = p.stem().string();

			nlp::sentence sent;
			sent.sent_id = sent_id;

			std::ifstream ifs(deppasmod.c_str());
			std::string buf, str;
			std::vector< std::string > lines;
			while ( getline(ifs, buf) ) {
				lines.push_back(buf);
			}

			switch (input_format) {
				case TF_DEP_CAB:
				case TF_PAS_SYN:
					sent.ma_tool = sent.MeCab;
					sent.parse_cabocha(lines);
					break;
				case TF_DEP_KNP:
				case TF_PAS_KNP:
					sent.parse_knp(lines);
					break;
				default:
					std::cerr << "ERROR: invalid input format" << std::endl;
					exit(-1);
			}
			learning_data.push_back(sent);
		}
	}
	

	void parser::load_xmls(std::vector< std::string > xmls, int input_format) {
		learning_data.clear();

		BOOST_FOREACH ( std::string xml_path, xmls ) {
			std::cout << xml_path << std::endl;
			boost::filesystem::path p(xml_path);
			std::string doc_id = p.stem().string();
			
			std::vector< std::vector< t_token > > oc_sents = parse_OC(xml_path);
			std::vector< nlp::sentence > parsed_sents;
			unsigned int sent_cnt = 0;
			BOOST_FOREACH ( std::vector< t_token > oc_sent, oc_sents ) {
				nlp::sentence mod_ipa_sent = make_tagged_ipasents( oc_sent, input_format );
				mod_ipa_sent.doc_id = doc_id;
				std::stringstream sent_id;
				sent_id << doc_id << "_" << std::setw(3) << std::setfill('0') << sent_cnt;
				sent_cnt++;
				mod_ipa_sent.sent_id = sent_id.str();
				learning_data.push_back(mod_ipa_sent);
			}
		}
	}


	void parser::learn(boost::filesystem::path *model_path_new, boost::filesystem::path *feat_path_new) {
		model_path = model_path_new;
		feat_path = feat_path_new;
		learn();
	}


	void parser::learn() {
		int num_node = 0;
		
		// count a number of instances for liblinear
		BOOST_FOREACH (nlp::sentence sent, learning_data) {
			BOOST_FOREACH (nlp::chunk chk, sent.chunks) {
				BOOST_FOREACH (nlp::token tok, chk.tokens) {
					if (detect_target(tok) && tok.has_mod) {
						num_node++;
					}
				}
			}
		}

		BOOST_FOREACH (unsigned int tag_id, analyze_tags) {
			linear::feature_node **x = new linear::feature_node*[num_node+1];
			int y[num_node+1];

			int node_cnt = 0;
			
			std::ofstream ofs(feat_path[tag_id].string().c_str());

			BOOST_FOREACH (nlp::sentence sent, learning_data) {
				BOOST_FOREACH (nlp::chunk chk, sent.chunks) {
					BOOST_FOREACH (nlp::token tok, chk.tokens) {
						if (detect_target(tok) && tok.has_mod) {
							std::string label = tok.mod.tag[id2tag(tag_id)];
							if (!l2i.exists_on_map(label)) {
								int lid = l2i.size()+1;
								l2i.set(label, lid);
								i2l.set(lid, label);
							}
							int label_id;
							l2i.get(label, &label_id);
							y[node_cnt] = label_id;

							std::stringstream tok_id_full;
							tok_id_full << sent.sent_id << "_" << chk.id << "_" << tok.id;

							feature_generator fgen(sent, tok.id);
							fgen.gen_feature_basic(3);
							fgen.gen_feature_function();
							fgen.gen_feature_dst_chunks();
							fgen.gen_feature_ttj(&dbr_ttj);

							//fgen.update(sent);
							switch (tag_id) {
								case TENSE:
									break;
								case ASSUMPTIONAL:
									break;
								case TYPE:
									fgen.gen_feature_mod("tense");
									fgen.gen_feature_fadic(&dbr_fadic);
									break;
								case AUTHENTICITY:
									fgen.gen_feature_mod("type");
									fgen.gen_feature_fadic(&dbr_fadic);
									break;
								case SENTIMENT:
									fgen.gen_feature_fadic(&dbr_fadic);
									break;
							}
							
							ofs << sent.sent_id << "(" << chk.id << "_" << tok.id << "): " << fgen.compile_feat_str(use_feats[tag_id]) << std::endl;

							t_feat feat = fgen.compile_feat(use_feats[tag_id]);
							t_feat::iterator it_feat;
							for (it_feat=feat.begin() ; it_feat!=feat.end() ; ++it_feat) {
								if (!f2i.exists_on_map(it_feat->first)) {
									f2i.set(it_feat->first, f2i.size()+1);
								}
							}

							linear::feature_node* xx = pack_feat_linear(feat);
							x[node_cnt] = xx;
							node_cnt++;
						}
					}
				}
			}
			
			ofs.close();

			linear::parameter _param;
			_param.solver_type = linear::L2R_LR;
			_param.eps = 0.01;
			_param.C = 1;
			_param.nr_weight = 0;
			_param.weight_label = new int(1);
			_param.weight = new double(1.0);

			linear::problem _prob;
			_prob.l = num_node;
			_prob.n = f2i.map.size()+1;
			_prob.y = y;
			_prob.x = x;
			_prob.bias = -1;

			linear::model *model;
			model = linear::train(&_prob, &_param);
			linear::save_model(model_path[tag_id].string().c_str(), model);
		}
	}


	nlp::sentence parser::make_tagged_ipasents( std::vector< t_token > sent_orig, int input_format ) {
		nlp::sentence sent;

		std::string text = "";
		BOOST_FOREACH ( t_token tok, sent_orig ) {
			text += tok.orthToken;
		}
		
		std::string parsed_text;
		switch (input_format) {
			case TF_XML_CAB:
			case TF_DEP_CAB:
			case TF_PAS_SYN:
				parsed_text = cabocha->parseToString( text.c_str() );
				sent.ma_tool = nlp::sentence::MeCab;
				sent.parse_cabocha(parsed_text);
				break;
			case TF_XML_KNP:
			case TF_DEP_KNP:
			case TF_PAS_KNP:
				{
					boost::filesystem::path tmp_knp("temp.knp");
					tmp_knp = TMP_DIR / tmp_knp;
					std::string com = "echo \"" + text + "\" | juman | knp -tab > " + tmp_knp.string();
#ifdef _MODEBUG
					std::cerr << com << std::endl;
#endif
					system(com.c_str());

					std::vector<std::string> lines;
					std::string buf;
					std::ifstream ifs(tmp_knp.string().c_str());
					while (ifs && getline(ifs, buf)) {
						lines.push_back(buf);
					}
					ifs.close();
					if (!boost::filesystem::remove(tmp_knp)) {
						std::cerr << "ERROR: cannot remove " << tmp_knp.string() << std::endl;
						exit(-1);
					}
					sent.parse_knp(lines);

					break;
				}
			default:
				std::cerr << "ERROR: invalid input format" << std::endl;
				exit(-1);
		}

#ifdef _MODEBUG
		std::vector<std::string> matchedIDs;
#endif
		int sp = 0, ep = 0;
		std::vector< nlp::chunk >::iterator it_chk;
		std::vector< nlp::token >::iterator it_tok;
		for (it_chk=sent.chunks.begin() ; it_chk!=sent.chunks.end() ; ++it_chk) {
			bool chk_has_mod = false;
			for (it_tok=it_chk->tokens.begin() ; it_tok!=it_chk->tokens.end() ; ++it_tok) {
				sp = ep;
				ep = sp + it_tok->surf.size() - 1;
#ifdef _MODEBUG
				std::cout << it_tok->surf << "(" << sp << "," << ep << ")" << std::endl;
#endif
				
				BOOST_FOREACH ( t_token tok, sent_orig ) {
					if (
							( tok.sp == sp && tok.ep == ep ) || 
							( tok.sp <= sp && ep <= tok.ep ) ||
							( sp <= tok.sp && tok.sp <= ep ) ||
							( sp <= tok.ep && tok.ep <= ep ) ||
							( sp <= tok.sp && tok.ep <= ep )
						 ) {
						
						if ( tok.eme.find("morphIDs") != tok.eme.end() ) {
							nlp::t_eme::iterator it_eme;
							for (it_eme=tok.eme.begin() ; it_eme!=tok.eme.end() ; ++it_eme) {
								if (it_eme->first == "source") {
									it_tok->mod.tag["source"] = it_eme->second;
								}
								else if (it_eme->first == "time") {
									it_tok->mod.tag["tense"] = it_eme->second;
								}
								else if (it_eme->first == "conditional") {
									it_tok->mod.tag["assumptional"] = it_eme->second;
								}
								else if (it_eme->first == "pmtype") {
									it_tok->mod.tag["type"] = it_eme->second;
								}
								else if (it_eme->first == "actuality") {
									it_tok->mod.tag["authenticity"] = it_eme->second;
								}
								else if (it_eme->first == "evaluation") {
									it_tok->mod.tag["sentiment"] = it_eme->second;
								}
								else if (it_eme->first == "focus") {
									it_tok->mod.tag["focus"] = it_eme->second;
								}
							}

							it_tok->mod.tids.push_back(it_tok->id);
							it_tok->has_mod = true;
							chk_has_mod = true;
#ifdef _MODEBUG
							std::cout << "found\t" << tok.orthToken << "(" << tok.morphID << ") - " << it_tok->surf << "(" << it_tok->id << ")" << std::endl;
							matchedIDs.push_back(tok.morphID);
#endif
						}
					}
				}
				ep++;
				sort(it_tok->mod.tids.begin(), it_tok->mod.tids.end());
				it_tok->mod.tids.erase(unique(it_tok->mod.tids.begin(), it_tok->mod.tids.end()), it_tok->mod.tids.end());
			}
			it_chk->has_mod = chk_has_mod;
		}
		
#ifdef _MODEBUG
		BOOST_FOREACH( t_token tok, sent_orig ) {
			if ( tok.eme.find("morphIDs") != tok.eme.end() && std::find(matchedIDs.begin(), matchedIDs.end(), tok.morphID) == matchedIDs.end() ) {
				std::cout << "not found\t" << tok.morphID << std::endl;
			}
		}
#endif
		
		return sent;
	}


	std::vector<t_token> parser::parse_bccwj_sent(tinyxml2::XMLElement *elemSent, int *sp) {
		std::vector<t_token> toks;
		
		if ( std::string(elemSent->Name()) == "sentence" || std::string(elemSent->Name()) == "quote" ) {
			tinyxml2::XMLElement *elem = elemSent->FirstChildElement("SUW");
			while (elem) {
				if (std::string(elem->Name()) == "SUW") {
					t_token tok;
					tok.orthToken = elem->Attribute("orthToken");
					tok.morphID = elem->Attribute("morphID");
					tok.sp = *sp;
					tok.ep = tok.sp + tok.orthToken.size() - 1;
					toks.push_back(tok);
					*sp = tok.ep + 1;
				}
				else if ( std::string(elem->Name()) == "sentence" || std::string(elem->Name()) == "quote" ) {
					BOOST_FOREACH(t_token tok, parse_bccwj_sent(elem, sp)) {
						toks.push_back(tok);
					}
				}
				elem = elem->NextSiblingElement();
			}
		}
		
		return toks;
	}


	std::vector< std::vector< t_token > > parser::parse_OC_sents (tinyxml2::XMLElement *elem) {
		std::vector< std::vector<t_token> > sents;
		
		tinyxml2::XMLElement *elemWL = elem->FirstChildElement("webLine");
		while (elemWL) {
			if (std::string(elemWL->Name()) == "webLine") {
				tinyxml2::XMLElement *elem = elemWL->FirstChildElement("sentence");
				while (elem) {
					if (std::string(elem->Name()) == "sentence") {
						int sp = 0;
						std::vector<t_token> sent = parse_bccwj_sent(elem, &sp);
						parse_modtag_for_sent(elem, &sent);
						sents.push_back(sent);
					}
					elem = elem->NextSiblingElement();
				}
			}
			elemWL = elemWL->NextSiblingElement();
		}
		
		return sents;
	}


	void parser::parse_modtag_for_sent(tinyxml2::XMLElement *elemSent, std::vector< t_token > *sent) {
		boost::regex reg_or("OR");
		boost::smatch m;

		while (elemSent) {
			tinyxml2::XMLElement *elemEME = elemSent->FirstChildElement("eme:event");
			while (elemEME) {
				if (std::string(elemEME->Name()) == "eme:event") {
					// event tag which has required tags, does not contain "OR" in morphIDs
					if (elemEME->Attribute("eme:orthTokens") &&
							elemEME->Attribute("eme:morphIDs") &&
							boost::regex_search(std::string(elemEME->Attribute("eme:morphIDs")), m, reg_or) == false )
					{
						bool use = true;
						if (elemEME->Attribute("eme:pseudo")) {
							std::string pseudo = elemEME->Attribute("eme:pseudo");
							if (pseudo == "機能語" || pseudo == "副詞" || pseudo == "連体詞" || pseudo == "名詞" || pseudo == "感嘆詞" || pseudo == "比況" || pseudo == "解析誤り") {
								use = false;
							}
							else if (pseudo == "名詞-事象可能") {
								use = false;
							}
						}

						if (use) {
							std::string orthToken = elemEME->Attribute("eme:orthTokens");
							std::vector<std::string> buf;
							boost::algorithm::split(buf, orthToken, boost::algorithm::is_any_of(","));
							orthToken = buf[0];

							std::string morphID = elemEME->Attribute("eme:morphIDs");
							boost::algorithm::split(buf, morphID, boost::algorithm::is_any_of(","));
							morphID = buf[buf.size()-1];

							std::vector< t_token >::iterator it_tok;
							for (it_tok=sent->begin() ; it_tok!=sent->end() ; ++it_tok) {
								if (it_tok->morphID == morphID) {
									const tinyxml2::XMLAttribute *attr = elemEME->FirstAttribute();
									while (attr) {
										std::string attr_name = attr->Name();
										boost::algorithm::split(buf, attr_name, boost::algorithm::is_any_of(":"));
										attr_name = buf[buf.size()-1];

										it_tok->eme[attr_name] = std::string(attr->Value());
										attr = attr->Next();
									}
								}
							}
						}
					}
				}
				elemEME = elemEME->NextSiblingElement();
			}
			elemSent = elemSent->NextSiblingElement();
		}

#ifdef _MODEBUG
		std::vector<std::string> attrs;
		attrs.push_back("source");
		attrs.push_back("time");
		attrs.push_back("conditional");
		attrs.push_back("pmtype");
		attrs.push_back("actuality");
		attrs.push_back("evaluation");
		BOOST_FOREACH( t_token tok, *sent ) {
			std::cout << tok.orthToken << " " << tok.morphID << " (" << tok.sp << "," << tok.ep << ")";
			boost::unordered_map< std::string, std::string >::iterator it;

			if (tok.eme.find("morphIDs") != tok.eme.end()) {
				std::cout << " " << tok.eme["morphIDs"];
				BOOST_FOREACH (std::string attr, attrs) {
					if (tok.eme.find(attr) != tok.eme.end()) {
						std::cout << " " << tok.eme[attr];
					}
				}
			}
			std::cout << std::endl;
		}
#endif
	}


	/*
	 * Convert modality-tagged OC xml into feature structure for ML
	 */
	std::vector< std::vector< t_token > > parser::parse_OC(std::string xml_path) {
		tinyxml2::XMLDocument doc;
		doc.LoadFile(xml_path.c_str());
		
		tinyxml2::XMLElement *elemQ = doc.FirstChildElement("sample")->FirstChildElement("OCQuestion");
		tinyxml2::XMLElement *elemA = doc.FirstChildElement("sample")->FirstChildElement("OCAnswer");
		std::vector< std::vector<t_token> > sentsQ;
		std::vector< std::vector<t_token> > sentsA;
		
		sentsQ = parse_OC_sents( elemQ );
//		parse_OC_modtag( elemQ, &sentsQ);
		sentsA = parse_OC_sents( elemA );
//		parse_OC_modtag( elemA, &sentsA);
		
		sentsQ.insert(sentsQ.end(), sentsA.begin(), sentsA.end());
	
		return sentsQ;
	}


	std::vector< std::vector< t_token > > parser::parse_OW_PB_PN(std::string xml_path) {
		tinyxml2::XMLDocument doc;
		doc.LoadFile(xml_path.c_str());
		
		tinyxml2::XMLElement *elem = doc.FirstChildElement("mergedSample");
		tinyxml2::XMLElement *elemSent = elem->FirstChildElement("sentence");
		std::vector< std::vector<t_token> > sents;
		while (elemSent) {
			if (std::string(elemSent->Name()) == "sentence") {
				int sp = 0;
				std::vector<t_token> sent = parse_bccwj_sent(elemSent, &sp);
				parse_modtag_for_sent(elemSent, &sent);
				sents.push_back(sent);
			}
			elemSent = elemSent->NextSiblingElement();
		}
		
		return sents;
	}


	template <typename K, typename V>
	inline void open_cdb(boost::filesystem::path cdb_path, CdbMap< K, V > *cdbmap) {
		boost::system::error_code error;
		bool res = boost::filesystem::exists(cdb_path, error);
		if (!res || error) {
		}
		else {
			cdbmap->open_cdb(cdb_path.string().c_str());
		}
	}

	void parser::open_f2i_cdb() {
		open_cdb(f2i_path, &f2i);
	}

	void parser::open_l2i_cdb() {
		open_cdb(l2i_path, &l2i);
	}

	void parser::open_i2l_cdb() {
		open_cdb(i2l_path, &i2l);
	}


	template <typename K, typename V>
	inline void save_cdb(boost::unordered_map< K, V > hash, boost::filesystem::path cdb_path, boost::filesystem::path dump_path) {
		std::ofstream ofs_dump(dump_path.string().c_str());
		std::ofstream ofs_cdb(cdb_path.string().c_str(), std::ios_base::binary);
		cdbpp::builder dbw(ofs_cdb);
		typename boost::unordered_map< K, V >::iterator it;
		for (it=hash.begin() ; it!=hash.end() ; ++it) {
			std::stringstream ss_key, ss_val;
			ss_key << it->first;
			ss_val << it->second;
			dbw.put(ss_key.str().c_str(), ss_key.str().length(), ss_val.str().c_str(), ss_val.str().length());
			ofs_dump << ss_key.str() << "\t" << ss_val.str() << std::endl;
		}
		ofs_dump.close();
	}
			
	void parser::save_f2i() {
		save_cdb( f2i.map, f2i_path, f2id_path );
	}

	void parser::save_l2i() {
		save_cdb( l2i.map, l2i_path, l2id_path );
	}

	void parser::save_i2l() {
		save_cdb( i2l.map, i2l_path, i2ld_path );
	}

};



