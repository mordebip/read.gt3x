% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/gt3x_unzip.R
\name{unzip_single_gt3x}
\alias{unzip_single_gt3x}
\title{Unzip a single gt3x file}
\usage{
unzip_single_gt3x(path, dirname = basename(gsub(".gt3x$| ", "", path)),
  location = tempdir(), files = c("info.txt", "log.bin"),
  remove_original = FALSE, check_structure = TRUE, verbose = TRUE)
}
\arguments{
\item{path}{Path to a .gt3x file}

\item{dirname}{The name of the resulting directory where the content of
 <path> are extracted.
Default is the name of the input file, sans the .gt3x extension.}

\item{location}{A path to an output directory. Default is a tempdir().}

\item{files}{The names of files to extract. Default is info.txt and log.bin}

\item{remove_original}{Remove the zipfile after unzipping?}

\item{check_structure}{check to see if the structure is right for the file}

\item{verbose}{print diagnostic messages}
}
\description{
A .gt3x file is a zipped archive with two files: log.bin and info.txt.
This function unzips the contents of the archive to a single folder.
This is a helper for unzip.gt3x()
}
