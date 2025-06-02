
lint:
	#cd ci && npm i && npm run lint
lint-fix:
	#cd ci && npm i && npm run lint-fix


configure:
	git submodule update --init --recursive
	rm -fr build || true
	cmake -DBUILD_STAGE=${STAGE} -DCMAKE_BUILD_TYPE=Release -S . -B build -G Ninja

build: configure
	cd build && ninja deploy

onboard-device:
	# creates nvram and loads onto target device
	if [ ! -e .venv ]; then python3 -m venv .venv; fi
	source .venv/bin/activate && pip3 install -q -r requirements.txt && python3 bin/flash_provisioning_nvram.py
