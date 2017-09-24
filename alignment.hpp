#include <complex>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <numeric>
#include <limits>
#include <thread>
#include <vector>

#include <fftw3.h>

//helper class to wrap fft in template
template <typename Real>
struct FFTW {static_assert(std::is_same<Real, float>::value || std::is_same<Real, double>::value || std::is_same<Real, long double>::value, "Real must be float, double, or long double");};

template<>
struct FFTW<float> {
	fftwf_plan pFor, pInv;
	FFTW(const int n, const unsigned int flag = FFTW_MEASURE) {//can use FFTW_ESTIMATE for small numbers of ffts
		std::vector<float> testSig(n);
		std::vector<fftwf_complex> testFft(n);
		pFor = fftwf_plan_dft_r2c_1d(n, testSig.data(), testFft.data(), flag);
		pInv = fftwf_plan_dft_c2r_1d(n, testFft.data(), testSig.data(), flag);
	}
	~FFTW() {
		fftwf_destroy_plan(pFor);
		fftwf_destroy_plan(pInv);
	}
	void forward(float* data, std::complex<float>* fft) const {fftwf_execute_dft_r2c(pFor, data               , (fftwf_complex*)fft);}
	void inverse(float* data, std::complex<float>* fft) const {fftwf_execute_dft_c2r(pInv, (fftwf_complex*)fft, data               );}
};

template<>
struct FFTW<double> {
	fftw_plan pFor, pInv;
	FFTW(const int n, const unsigned int flag = FFTW_MEASURE) {
		std::vector<double> testSig(n);
		std::vector<fftw_complex> testFFt(n);
		pFor = fftw_plan_dft_r2c_1d(n, testSig.data(), testFFt.data(), flag);
		pInv = fftw_plan_dft_c2r_1d(n, testFFt.data(), testSig.data(), flag);
	}
	~FFTW() {
		fftw_destroy_plan(pFor);
		fftw_destroy_plan(pInv);
	}
	void forward(double* data, std::complex<double>* fft) const {fftw_execute_dft_r2c(pFor, data              , (fftw_complex*)fft);}
	void inverse(double* data, std::complex<double>* fft) const {fftw_execute_dft_c2r(pInv, (fftw_complex*)fft, data              );}
};

template<>
struct FFTW<long double> {
	fftwl_plan pFor, pInv;
	FFTW(const int n, const unsigned int flag = FFTW_MEASURE) {
		std::vector<long double> testSig(n);
		std::vector<fftwl_complex> testFft(n);
		pFor = fftwl_plan_dft_r2c_1d(n, testSig.data(), testFft.data(), flag);
		pInv = fftwl_plan_dft_c2r_1d(n, testFft.data(), testSig.data(), flag);
	}
	~FFTW() {
		fftwl_destroy_plan(pFor);
		fftwl_destroy_plan(pInv);
	}
	void forward(long double* data, std::complex<long double>* fft) const {fftwl_execute_dft_r2c(pFor, data               , (fftwl_complex*)fft);}
	void inverse(long double* data, std::complex<long double>* fft) const {fftwl_execute_dft_c2r(pInv, (fftwl_complex*)fft, data               );}
};

//@brief: compute the upsampled fft value for a single subpixel
//@param kernel: upsampling kernel for subpixel
//@param xCorr: fft of cross correlation
//@return: upsampled fft value
template <typename Real>
inline Real upsampledValue(const std::vector< std::complex<Real> > & k, const std::vector< std::complex<Real> >& xCorr) {
	//abs(dot(2 conjugate symmetric values)) -> all complex components cancel (center complex component doesn't but it is vanishingly small compared to real part for relevant sizes)
	//to be fully rigourous could check for even length half cross correlations and handle)
	return std::inner_product(xCorr.begin()+1, xCorr.end(), k.begin()+1, Real(0), std::plus<Real>(), [](const std::complex<Real>& a, const std::complex<Real>& b){
		return a.real() * b.real() - a.imag() * b.imag();//only accumulate the real part since the imaginary part will cancel out from conjugate symmetry
	}) * Real(2) + xCorr.front().real();//multiply by 2 to account for symmetry and add first entry (k[0] is always 1)
}

//@brief: compute the highest correlation sub pixel shift
//@param kernel: upsampling kernel
//@param xCorr: fft of cross correlation
//@param shift: initial search position in upsampled kernel (relative to kernel center)
//@return: highest correlation sub pixel shift (relative to kernel center)
template <typename Real>
inline int computeSubpixelShift(const std::vector< std::vector< std::complex<Real> > >& kernel, const std::vector< std::complex<Real> >& xCorr, int shift = 0) {
	//this operation is relatively expensive to brute force and for dic speckle the cross correlation is well behaved for small shifts, so a linear search should work well
	const size_t kernelSize = (kernel.size() + 1) / 2;
	Real negCor = upsampledValue(kernel[kernelSize-1+shift-1], xCorr);//compute cross correlation for single sub pixel shift in negative direction
	Real maxCor = upsampledValue(kernel[kernelSize-1+shift  ], xCorr);//compute cross correlation for previous sub pixel shift
	Real posCor = upsampledValue(kernel[kernelSize-1+shift+1], xCorr);//compute cross correlation for single sub pixel shift in positive direction
	if(negCor > maxCor || posCor > maxCor) {
		const bool neg = negCor > posCor;//determine which direction to search in
		Real curCor = neg ? negCor : posCor;
		neg ? --shift : ++shift;
		while(curCor > maxCor) {//search until the maximum is passed
			maxCor = curCor;
			neg ? --shift : ++shift;
			if(shift == kernelSize || -shift == kernelSize) throw std::runtime_error("maxima not found within window");//the end of the window is reached
			curCor = upsampledValue(kernel[kernelSize-1+shift], xCorr);//compute cross correlation for single sub pixel shift in positive direction
		}
		neg ? ++shift : --shift;//walk back to maxima
	}
	return shift;
}

//@brief: compute the highest correlation sub pixel shift for each row, average, and apply the result
//@param frame: the frame to align
//@param refFrame: conj(fft(frame to align to))
//@param inds: fft shifted intds (0, 1, 2, 3, ..., cols/2, -cols/2, 1-cols/2, ..., -3, -2, -1)
//@param kernel: upsampling kernel
//@param cols: frame width
//@param rows: frame height
//@param snake: true/false if rows have the same / alternating shift
//@param upsampleFactor: sub pixel resolution factor
//@return: the applied shift
template <typename Real, typename T>
inline Real alignFrame(std::vector<T>& frame, const std::vector< std::complex<Real> >& refFrame, const std::vector<int>& inds, const std::vector< std::vector< std::complex<Real> > >& kernel, const int cols, const int rows, const bool snake, const int upsampleFactor, const FFTW<Real>& fftw) {
	//compute fft of each row of moving frame
	const int fftSize = cols / 2 + 1;
	const int fftSizePad = (cols + 2) / 1;//odd size offsets can cause fftw to crash or prevent use of SIMD instructions
	std::vector<Real> rowData(cols);
	std::vector< std::complex<Real> > movFrame;
	movFrame.reserve(fftSizePad * rows);
	for(int i = 0; i < rows; i++) {
		std::copy(frame.begin() + i * cols, frame.begin() + (i+1) * cols, rowData.begin());//copy data to Real
		fftw.forward(rowData.data(), movFrame.data() + i * fftSizePad);//compute fft
	}

	//upsample convolved ffts near origin to find best shift for each row
	int shift = 0;//search from zero on first row
	Real meanShift = 0.0;
	std::vector< std::complex<Real> > xCorr(fftSize);
	for(int i = 0; i < rows; i++) {
		std::transform(refFrame.begin() + i * fftSizePad, refFrame.begin() + i * fftSizePad + fftSize, movFrame.data() + i * fftSizePad, xCorr.begin(), std::multiplies< std::complex<Real> >());//first half of cross correlation
		shift = computeSubpixelShift(kernel, xCorr, snake ? -shift : shift);//search from previous result on subsequent rows
		meanShift += (snake && 1 == i % 2) ? -shift : shift;
	}
	meanShift /= rows * upsampleFactor;//fftw using a different convention that I was

	//apply shift
	const Real vMin(std::numeric_limits<T>::lowest());
	const Real vMax(std::numeric_limits<T>::max());
	const Real k = Real(-6.2831853071795864769252867665590057683943387987502 * meanShift) / cols;
	std::vector< std::complex<Real> > phaseShift(fftSize);
	std::transform(inds.begin(), inds.end(), phaseShift.begin(), [k](const int& x){return std::complex<Real>(std::cos(k*x), std::sin(k*x));});
	if(snake) {
		for(int i = 0; i < rows; i+=2) std::transform(phaseShift.begin(), phaseShift.end(), movFrame.data() + i * fftSizePad, movFrame.data() + i * fftSizePad, std::multiplies< std::complex<Real> >());
		std::for_each(phaseShift.begin(), phaseShift.end(), [](std::complex<Real>& v){v = std::conj(v);});
		for(int i = 1; i < rows; i+=2) std::transform(phaseShift.begin(), phaseShift.end(), movFrame.data() + i * fftSizePad, movFrame.data() + i * fftSizePad, std::multiplies< std::complex<Real> >());
	} else {
		for(int i = 0; i < rows; i++) std::transform(phaseShift.begin(), phaseShift.end(), movFrame.data() + i * fftSizePad, movFrame.data() + i * fftSizePad, std::multiplies< std::complex<Real> >());
	}
	for(int i = 0; i < rows; i++) {
		fftw.inverse(rowData.data(), movFrame.data() + i * fftSizePad);//compute inverse fft
		std::transform(rowData.begin(), rowData.end(), frame.begin() + i * cols, [cols, vMin, vMax](const Real&v){return (T)std::max(vMin, std::min(vMax, std::round(v / cols)));});//scale (fftw doesn't scale) and clamp to pixel range
	}
	return -meanShift;//fftw convention
}

template <typename Real, typename T>
inline void alignFrames(std::vector< std::vector<T> >& frames, const std::vector< std::complex<Real> >& refFrame, const std::vector<int>& inds, const std::vector< std::vector< std::complex<Real> > >& kernel, const int cols, const int rows, const bool snake, const int upsampleFactor, std::vector<Real>& shifts, const FFTW<Real>& fftw, int const * const bounds, std::exception_ptr& pExp) {
	try {
		for(int i = bounds[0]; i < bounds[1]; i++) shifts[i-1] = alignFrame(frames[i-1], refFrame, inds, kernel, cols, rows, snake, upsampleFactor, fftw);
	} catch (...) {
		pExp = std::current_exception();
	}
}

template <typename Real, typename T>
std::vector<Real> correlateRows(std::vector< std::vector<T> >& frames, const int rows, const int cols, const bool snake = true, const Real maxShift = 1.5, const int upsampleFactor = 16) {
	//compute fft timeings onces
	const FFTW<Real> fftw(cols);//copmpute timings once
	const int fftSize = cols / 2 + 1;
	const int fftSizePad = (cols + 2) / 1;//odd size offsets can cause fftw to crash or prevent use of SIMD instructions

	//"Efficient subpixel image registration algorithms," Opt. Lett. 33, 156-158 (2008).
	//compute upsampling kernel for shifts of -maxShift->0->maxShift, modified to account for conjugate symmetry
	std::vector<int> inds(fftSize);
	std::iota(inds.begin(), inds.end(), 0);
	if(0 == cols % 2) inds.back() = -inds.back();
	const int kernelSize = (int) std::ceil(maxShift * upsampleFactor);
	std::vector< std::vector< std::complex<Real> > > kernel(2 * kernelSize - 1);
	const Real kExp = Real(-6.2831853071795864769252867665590057683943387987502) / (cols * upsampleFactor);
	for(int i = 0; i < kernelSize; i++) {
		const Real k = kExp * i;
		kernel[kernelSize - 1 - i].reserve(fftSize);
		kernel[kernelSize - 1 + i].reserve(fftSize);
		std::transform(inds.begin(), inds.end(), std::back_inserter(kernel[kernelSize - 1 + i]), [k](const int& x){return std::complex<Real>(std::cos(k*x), std::sin(k*x));});
		if(i > 0) std::transform(kernel[kernelSize - 1 + i].begin(), kernel[kernelSize - 1 + i].end(), std::back_inserter(kernel[kernelSize - 1 - i]), [](const std::complex<Real>& v){return std::conj(v);});
	}

	//compute fft of each row of final frame
	std::vector<Real> rowData(cols);
	std::vector< std::complex<Real> > refFrame(fftSizePad * rows);
	for(int i = 0; i < rows; i++) {
		std::copy(frames.back().begin() + i * cols, frames.back().begin() + (i+1) * cols, rowData.begin());//copy data to Real
		fftw.forward(rowData.data(), refFrame.data() + i * fftSizePad);//compute fft (Note: the 2nd argument here can be seen as a pointer)
	}
	for(std::complex<Real>& v : refFrame) v = std::conj(v);//need complex conjugate of reference fft

	static const bool parallel = true;
	if(parallel) {
		//build indicies of threads
		const size_t threadCount = std::max<size_t>(std::thread::hardware_concurrency(), 1);
		std::vector<int> workerInds(threadCount + 1);
		std::vector<std::exception_ptr> expPtrs(threadCount, NULL);
		const Real frameCount = Real(frames.size()) / threadCount;
		for(size_t i = 0; i < workerInds.size(); i++) workerInds[i] = (int)std::round(frameCount * i);
		std::replace(workerInds.begin(), workerInds.end(), 0, 1);

		//compute and apply subpixel shift for each frame in parallel
		std::vector<Real> frameShifts(frames.size());
		std::vector<std::thread> workers((size_t)threadCount);
		for(size_t i = 0; i < workers.size(); i++) workers[i] = std::thread(alignFrames<Real, T>, std::ref(frames), std::ref(refFrame), std::ref(inds), std::ref(kernel), cols, rows, snake, upsampleFactor, std::ref(frameShifts), std::ref(fftw), workerInds.data() + i, std::ref(expPtrs[i]));
		for(size_t i = 0; i < workers.size(); i++) workers[i].join();
		for(size_t i = 0; i < workers.size(); i++)
			if(NULL != expPtrs[i]) std::rethrow_exception(expPtrs[i]);
		return frameShifts;
	} else {
		std::vector<Real> frameShifts(frames.size());
		for(int i = 1; i < frames.size(); i++) frameShifts[i-1] = alignFrame(frames[i-1], refFrame, inds, kernel, cols, rows, snake, upsampleFactor, fftw);//serial
		return frameShifts;
	}
}