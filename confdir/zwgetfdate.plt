%% $Id: zwgetfdate.plt,v 1.1 2015/11/25 07:51:35 grimm Exp $
%% Copyright 2008 Z. Wagner, http://icebearsoft.euweb.cz
%
% This work may be distributed and/or modified under the
% conditions of the LaTeX Project Public License, either version 1.3
% of this license or (at your option) any later version.
% The latest version of this license is in
%   http://www.latex-project.org/lppl.txt
% and version 1.3 or later is part of all distributions of LaTeX
% version 2005/12/01 or later.
%
% This work has the LPPL maintenance status `maintained'.
% 
% The Current Maintainer of this work is Z. Wagner.
%
% This work consists of the files: zwgetfdate.sty and
% the documentation files zwgetfdate.tex, zwgetfdate.pdf.

\let\ZW@ProvidesPackage\ProvidesPackage
\let\ZW@ProvidesFile\ProvidesFile

\def\ProvidesPackage{\let\ZW@ProvidesWhat\ZW@ProvidesPackage \zw@provides}
\def\ProvidesFile{\let\ZW@ProvidesWhat\ZW@ProvidesFile \zw@provides}

\def\zw@provides#1{\def\ZW@StoredName{#1}\zw@get@optarg}
\newcommand*\zw@get@optarg[1][]{\def\zw@temp{#1}\ifx\zw@temp\@empty
     \def\zw@next{\ZW@ProvidesWhat{\ZW@StoredName}}%
   \else
     \expandafter\expandafter\expandafter\zw@getfdate\expandafter\zw@temp\space\@@@ZW
     \def\zw@next{\ZW@ProvidesWhat{\ZW@StoredName}[#1]}%
   \fi
   \zw@next}

\def\zw@fdate@prefix#1#2{\def\ZW@fdate@prefix{ZW@DateOf.#1@#2}}
\def\ZW@date@namedef#1#2{\zw@fdate@prefix{#1}{#2}\@namedef{\ZW@fdate@prefix}}

\def\zw@getfdate #1 #2\@@@ZW{%
  \def\zw@internal ##1/##2/##3/##4\@@@ZW{%
    \def\zw@temp{##3}\ifx\zw@temp\@empty\else
    \ifx\ZW@ProvidesWhat\ZW@ProvidesPackage
      \def\zw@temp{package}%
    \else
      \def\zw@temp{file}%
    \fi
    \ZW@date@namedef{\zw@temp}{\ZW@StoredName}{##1/##2/##3}\fi}%
  \zw@internal #1///\@@@ZW}

\ProvidesPackage{zwgetfdate}[2008/07/14 Get File/Package Date]

\DeclareRobustCommand\DateOfPackage{\ZW@DateOf{package}}
\DeclareRobustCommand\DateOfFile{\ZW@DateOf{file}}

\def\ZW@DateOf#1#2{\zw@fdate@prefix{#1}{#2}\expandafter\ifx\csname\ZW@fdate@prefix\endcsname\relax
    \PackageWarning{zwgetfdate}{Date of #1 `#2' is not available}\else
    \@nameuse{\ZW@fdate@prefix}\fi}
