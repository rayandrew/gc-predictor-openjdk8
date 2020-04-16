#!/usr/bin/env bash

echo "Installing dependencies"

sudo apt update
sudo apt install -y software-properties-common
sudo add-apt-repository ppa:openjdk-r/ppa \
    && sudo apt-get update -q
sudo apt install -y git mercurial zip bzip2 unzip tar curl autoconf
sudo apt install -y ccache make gcc g++ ca-certificates ca-certificates-java
sudo apt install -y libx11-dev libxext-dev libxrender-dev libxtst-dev libxt-dev libxrandr-dev libxt-dev libfontconfig1-dev
sudo apt install -y libasound2-dev libcups2-dev libfreetype6-dev
sudo apt install -y build-essential ruby ruby-dev
sudo apt install -y openjdk-8-jdk
sudo apt install -y pkg-config
sudo gem install fpm

echo "Done installing dependencies"
