The Simpsons: Hit & Run on Apple TV
Overview

This is a port of The Simpsons: Hit & Run to Apple TV. The source code for this project originates from the ZenoArrows repository.
Steps to Set Up
Place Game Assets:
Put the game data (assets) into the assets folder. Ensure all required resources are in the correct place.

Install Premake5 via Homebrew:
If you don't have Premake5 installed, use Homebrew.
Run:
brew install premake5  
Generate the Xcode Project:
In the project directory, generate the Xcode project by running:
premake5 xcode4  

Once generated, open the Xcode project, build, and deploy to your Apple TV!
Known Bugs

Lisa’s school environment is visually broken or glitchy in certain areas.
Non-player characters’ speech can play back too fast in certain scenes.
Some windows or text boxes have oversized text, making them harder to read.