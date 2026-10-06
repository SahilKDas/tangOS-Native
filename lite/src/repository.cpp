#include "repository.h"
#include <stdexcept>
#include <sstream>
#include <algorithm>
namespace lite {
Repository::Repository(Runner&r,const fs::path&p,const Settings&s):runner(r),settings(s){auto x=runner.run({{"git","rev-parse","--show-toplevel"},p});if(x.code)throw std::runtime_error("Select a Git working tree (git must be on PATH).\n"+x.output); // Runner's capture includes command banner: use raw capture below.
    auto a=x.output.find('\n',x.output.find("Working directory:"));auto b=x.output.find("\n[",a+1);auto raw=trim(x.output.substr(a+1,b-a-1));root=fs::u8path(raw);if(!fs::is_directory(root))throw std::runtime_error("Git returned invalid root: "+raw);
}
std::string Repository::git(const Args&a){Args cmd{"git","--no-pager","-c","core.quotePath=false","-c","color.ui=false","-c","core.fsmonitor=false"};cmd.insert(cmd.end(),a.begin(),a.end());auto r=runner.run({cmd,root});if(r.code)throw std::runtime_error(r.output);auto x=r.output.find('\n',r.output.find("Working directory:"));auto y=r.output.rfind("\n[");return r.output.substr(x+1,y-x-1);}
std::string Repository::status(){std::string t="Repository: "+utf8(root.wstring())+"\r\n\r\n";t+="BRANCH / CHANGES\r\n"+git({"status","--short","--branch"});t+="\r\nBRANCHES\r\n"+git({"branch","-avv"});t+="\r\nREMOTES (Tango, SCOPIC64 and forks use their configured names)\r\n"+git({"remote","-v"});t+="\r\nWORKTREES\r\n"+git({"worktree","list","--porcelain"});auto changes=parseStatus(git({"status","--porcelain=v1","-z"}));t+="\r\nSAFETY\r\n";for(auto&c:changes){if(c.conflict())t+="CONFLICT "+c.path+" - resolve, stage, and continue in Git.\r\n";auto why=blockedPath(c.path,settings);if(!why.empty())t+="EXCLUDED "+c.path+": "+why+"\r\n";}t+="Port-only mode: "+std::string(settings.portOnly?"ON":"OFF (src commits need byte proof)")+"\r\n";return t;}
namespace { void requirePath(const std::string&p,const Settings&s){auto why=blockedPath(p,s);if(!why.empty())throw std::runtime_error("Blocked "+p+": "+why);} }
std::string Repository::safetyIndex(){auto conflicts=git({"ls-files","-u"});if(!conflicts.empty())throw std::runtime_error("Unresolved conflicts: resolve files before committing.\n"+conflicts);
    auto paths=split(git({"diff","--cached","--name-only","--no-renames","-z"}),'\0');if(paths.empty())throw std::runtime_error("Nothing staged. Stage explicit paths, then preview the commit.");
    for(auto&p:paths){requirePath(p,settings);auto ignored=runner.run({{"git","check-ignore","--no-index","--",p},root});if(ignored.code==0)throw std::runtime_error("Blocked ignored local asset: "+p);if(ignored.code!=1)throw std::runtime_error("Ignore check failed: "+ignored.output);
        auto listing=git({"ls-files","--stage","-z","--",p});if(listing.empty())continue;auto space=listing.find(' ');auto oid=listing.substr(space+1,listing.find(' ',space+1)-space-1);if(listing.rfind("120000",0)==0||listing.rfind("160000",0)==0)throw std::runtime_error("Symlinks/submodules require external review: "+p);auto blob=git({"cat-file","blob",oid});auto why=blockedBlob(blob);if(!why.empty())throw std::runtime_error("Blocked "+p+": "+why);
    }return git({"write-tree"});
}
std::string Repository::commitPreview(){auto tree=safetyIndex();return "INDEX TREE: "+trim(tree)+"\n"+git({"diff","--cached","--stat"})+"\n"+git({"diff","--cached","--no-ext-diff","--no-textconv","--binary"});}
std::string Repository::pushPreview(const std::string&remote,const std::string&branch){if(!validRef(remote)||remote.find('/')!=remote.npos||!validRef(branch))throw std::runtime_error("Supply a configured remote name and destination branch, without options.");git({"remote","get-url","--push",remote});auto target="refs/remotes/"+remote+"/"+branch;
    // Requiring a fetched destination closes the 'new branch skips historical blobs' gap.
    git({"rev-parse","--verify",target});std::string range=target+"..HEAD";auto ids=split(git({"rev-list","--reverse",range}),'\n');if(ids.empty())throw std::runtime_error("No outgoing commits. Fetch first if remote state changed.");std::string out="DESTINATION: "+remote+" / "+branch+"\nHEAD: "+trim(git({"rev-parse","HEAD"}))+"\n";for(auto id:ids){id=trim(id);if(id.empty())continue;auto paths=split(git({"diff-tree","--root","-m","--no-commit-id","--name-only","--no-renames","-r","-z",id}),'\0');for(auto&p:paths){requirePath(p,settings);auto tree=git({"ls-tree","-z",id,"--",p});if(tree.empty())continue;auto sp=tree.find(' '),sp2=tree.find(' ',sp+1),tab=tree.find('\t');if(tree.substr(sp+1,sp2-sp-1)!="blob"||tree.rfind("120000",0)==0)throw std::runtime_error("External review required for outgoing symlink/submodule: "+p);auto why=blockedBlob(git({"cat-file","blob",tree.substr(sp2+1,tab-sp2-1)}));if(!why.empty())throw std::runtime_error("Blocked outgoing "+p+": "+why);auto ignored=runner.run({{"git","check-ignore","--no-index","--",p},root});if(ignored.code==0)throw std::runtime_error("Blocked outgoing ignored asset: "+p);if(ignored.code!=1)throw std::runtime_error("Ignore safety check failed");}
        out+=git({"show","--format=fuller","--stat","--patch","--no-ext-diff","--no-textconv","--binary",id})+"\n";
    }return out;
}
std::string Repository::agentHandoff(){std::string out="TangOS Lite agent handoff\nRepository: "+utf8(root.wstring())+"\nPort-only: "+(settings.portOnly?std::string("true"):"false")+"\nNever modify src/ for port-only fixes. Never stage ROMs, extracted assets, credentials, ignored or locally excluded files. Preview each commit before push. Read scoped AGENTS.md before editing. Use the repository's ownership, independent verification and handoff protocol; do not duplicate another agent's claim.\n\n";
    auto files=split(git({"ls-files","-z","--","AGENTS.md","**/AGENTS.md","notes/agents/README.md"}),'\0');if(fs::exists(root/"AGENTS.md")&&std::find(files.begin(),files.end(),"AGENTS.md")==files.end())files.insert(files.begin(),"AGENTS.md");for(auto&p:files)out+="\n--- "+p+" ---\n"+read(root/fs::u8path(p));if(files.empty())out+="No AGENTS.md found. Establish coordination rules before starting multiple agents.\n";return out;
}
Command Repository::action(const std::string&name,const std::string&remote,const std::string&ref,const std::string&text){Args a{"git","--no-pager","-c","color.ui=false"};auto append=[&](Args b){a.insert(a.end(),b.begin(),b.end());};
    auto clean=[&](){if(!git({"status","--porcelain=v1","-z"}).empty())throw std::runtime_error("Operation requires a clean working tree. Commit/stash changes outside Lite first.");};
    auto reference=[&](){if(!validRef(ref))throw std::runtime_error("Invalid ref; use a branch like tango/main.");};
    if(name=="Fetch")append({"fetch","--all"});
    else if(name=="Pull (fast-forward)"){clean();append({"pull","--ff-only"});}
    else if(name=="Merge"){reference();clean();append({"merge","--no-edit",ref});}
    else if(name=="Rebase"){reference();clean();append({"rebase",ref});}
    else if(name=="Stage paths"){Args paths;for(auto p:split(text,'\n')){p=trim(p);if(p.empty())continue;requirePath(p,settings);paths.push_back(p);}if(paths.empty())throw std::runtime_error("Enter one exact relative path per line in Details.");append({"--literal-pathspecs","add","--"});a.insert(a.end(),paths.begin(),paths.end());}
    else if(name=="Commit staged"){if(trim(text).empty())throw std::runtime_error("Enter a commit message in Details.");safetyIndex();append({"commit","-m",text});}
    else if(name=="Push reviewed"){if(!validRef(remote)||remote.find('/')!=remote.npos)throw std::runtime_error("Invalid remote name");reference();append({"push",remote,"HEAD:refs/heads/"+ref});}
    else if(name=="Compare upstreams"){reference();if(!validRef(remote))throw std::runtime_error("Remote field must contain first ref, e.g. tango/main");append({"log","--left-right","--graph","--oneline",remote+"..."+ref});}
    else if(name=="Upstream diff"){reference();if(!validRef(remote))throw std::runtime_error("Invalid first ref");append({"diff","--no-ext-diff","--no-textconv",remote,ref,"--"});}
    else if(name=="Add remote"){if(!validRef(remote)||remote.find('/')!=remote.npos)throw std::runtime_error("Supply a remote name");if(text.rfind("https://github.com/",0)!=0&&text.rfind("git@github.com:",0)!=0)throw std::runtime_error("Use a GitHub HTTPS/SSH URL in Details");if(text.find_first_of("\r\n\t ")!=text.npos||(text.find('@',text.find("://")+3)!=text.npos&&text.rfind("https://",0)==0))throw std::runtime_error("Remote URL must not contain credentials/whitespace");append({"remote","add",remote,text});}
    else if(name=="PR readiness"){return {{"gh","pr","view","--json","url,state,isDraft,mergeable,mergeStateStatus,reviewDecision,statusCheckRollup,headRefOid"},root};}
    else if(name=="PR checks"){return {{"gh","pr","checks"},root};}
    else if(name=="Create draft PR"){
        reference();if(text.empty())throw std::runtime_error("Details must contain a PR title");
        auto parts=split(remote,'/');if(parts.size()!=2||!validRef(parts[0])||!validRef(parts[1]))throw std::runtime_error("Remote field must be target owner/repo");
        auto branch=trim(git({"branch","--show-current"}));if(!validRef(branch))throw std::runtime_error("PR creation requires an attached branch");
        // origin identifies the user's fork; the target can be either upstream.
        auto url=trim(git({"remote","get-url","--push","origin"}));std::string owner;
        for(auto prefix:{std::string("https://github.com/"),std::string("git@github.com:")})if(url.rfind(prefix,0)==0)owner=url.substr(prefix.size(),url.find('/',prefix.size())-prefix.size());
        if(!validRef(owner)||owner.find('/')!=owner.npos)throw std::runtime_error("origin must identify a GitHub fork; configure it before creating a PR");
        return {{"gh","pr","create","--repo",remote,"--base",ref,"--head",owner+":"+branch,"--title",text,"--body","Created with TangOS Lite. Review changes and all required checks before merging.","--draft"},root};
    }
    else throw std::runtime_error("Unknown action");
    return {a,root};
}
Command Repository::check(size_t i){auto v=discoverChecks(root,settings);if(i>=v.size()||!v[i].available)throw std::runtime_error("Check unavailable: repository script or build output missing. See required dependencies.");auto c=v[i].command;if(c.argv[0]=="cmd.exe"){auto p=utf8(root.wstring());if(p.find_first_of("&|<>^%!\r\n\"")!=p.npos)throw std::runtime_error("Port CMD wrapper refuses shell metacharacters in repository path");}return c;}
}
