#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include "sentence.hpp"
#include "modality.hpp"

int main(int argc, char *argv[]) {

	boost::program_options::options_description opt("Usage", 200);
	opt.add_options()
		("file,f", boost::program_options::value<std::string>(), "input xml file (required)")
		("outdir,o", boost::program_options::value<std::string>(), "output directory (optional): default same as input xml file")
		("enable-wl", "enabled parsing webLine tag")
		("help,h", "Show help messages")
		("version,v", "Show version informaion");

	boost::program_options::variables_map argmap;
	boost::program_options::store(parse_command_line(argc, argv, opt), argmap);
	boost::program_options::notify(argmap);
	
	if (argmap.count("help")) {
		std::cout << opt << std::endl;
		return 1;
	}

	if (argmap.count("version")) {
		std::cout << PACKAGE_VERSION << std::endl;
		return 1;
	}

	std::string xml;
	if (argmap.count("file")) {
		xml = argmap["file"].as<std::string>();
	}
	else {
		std::cerr << "error: input xml file is required" << std::endl;
		exit(-1);
	}
	std::cerr << "input: " << xml << std::endl;
	boost::filesystem::path xml_path(xml);

	std::string outdir;
	if (argmap.count("outdir")) {
		outdir = argmap["outdir"].as<std::string>();
	}
	else {
		outdir = xml_path.parent_path().string();
	}
	std::cerr << "outdir: " << outdir << std::endl;
	boost::filesystem::path outdir_path(outdir);

	modality::parser mod_parser;

	std::vector< std::vector< modality::t_token > > sents;
	if (argmap.count("enable-wl")) {
		sents = mod_parser.parse_OC(xml_path.string());
	}
	else {
		sents = mod_parser.parse_OW_PB_PN(xml_path.string());
	}

	int cnt = 0;
	BOOST_FOREACH ( std::vector< modality::t_token > sent, sents ) {
		nlp::sentence *mod_ipa_sent = new nlp::sentence;
		mod_parser.make_tagged_ipasents(sent, modality::IN_DEP_CAB, *mod_ipa_sent);

		std::stringstream ss;
		ss << (outdir_path / xml_path.stem()).string() << "_" << std::setw(3) << std::setfill('0') << cnt << ".depmod";
		std::cerr << "output: " << ss.str() << std::endl;
		std::ofstream os(ss.str().c_str());
		std::string cabocha_str;
		mod_ipa_sent->cabocha(cabocha_str);
		os << cabocha_str << std::endl;
		os.close();
		cnt++;

		delete mod_ipa_sent;
	}

	return true;
}

