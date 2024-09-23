#! /bin/bash

# Ensure you have gh-pages installed globally
echo "Removing tmp/ folder with"
rm -rf tmp

echo "Regenerating docs ..."
doxygen Doxyfile 

cd tmp || exit

echo "Moving html/ & xml/ files into root..."
cd html && cp -Rp . .. && cd ..
cd xml && cp -Rp . .. && cd ..

echo "Clean up..."
rm -rf html xml

cd ..

echo "Publishing docs to gh-pages!"
gh-pages -d tmp

