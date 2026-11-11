# Lab 5: Parallel Applications

You can run any `make` target from either the repository root (the top-level `makefile` forwards to `src/`) or inside `src/`.

Default build/run variables (defined in `src/makefile`):
- `INPUT=images/1.ppm` (`IMG_DIR=images`, `IMAGE=1`)
- `ITERS=1000`
- `PORT=12345`
- `IP=127.0.0.1`

---

## Part III: Running Across Two Computers (TCP Sockets)

### IP & Port Convention
The assignment specification does not prescribe a discovery mechanism for Computer A's IP address. Our convention is:
- **Computer A** acts as the TCP server (listening on `INADDR_ANY` at `PORT`, default `12345`).
- **Computer B** acts as the TCP client and connects to `<IP>:<PORT>`, passed via `make` as `IP=<A's IP>` (and optionally `PORT=<port>` if changed from `12345`). Computer B retries connecting for up to 60 seconds (100 ms intervals) if started slightly before A.

### 1. Find Computer A's IP and Open the Firewall
On **Computer A**, determine its IP address on the shared network:
```bash
ip a
```
If `ufw` is active on Computer A (or B), allow TCP traffic on the chosen port before running:
```bash
sudo ufw allow 12345/tcp
```

### 2. Run Part III.1 (`A = S1 + S2`, `B = S3`)
Replace `X` with `1` (Variant 1) or `2` (Variant 2):

- On **Computer A**:
  ```bash
  make part3_1_variantX_A
  ```
- On **Computer B**:
  ```bash
  make part3_1_variantX_B IP=<A's IP>
  ```
This writes `src/output_part3_1_variantX.ppm` on Computer B.

### 3. Run Part III.2 (`A = S1`, `B = S2 + S3`)
Replace `X` with `1` (Variant 1) or `2` (Variant 2):

- On **Computer A**:
  ```bash
  make part3_2_variantX_A
  ```
- On **Computer B**:
  ```bash
  make part3_2_variantX_B IP=<A's IP>
  ```
This writes `src/output_part3_2_variantX.ppm` on Computer B.

*(If overriding `PORT`, pass the same `PORT=<port>` to both A and B, and open that port in `ufw`.)*

---

## Other Makefile Targets

- `make part1` — compiles and runs Part I (`src/output_part1.ppm`).
- `make part2_1_variant1` / `make part2_1_variant2` — Part II.1 (3 threads + atomics); creates `src/output_part2_1_variantX.ppm` (and copies to `src/output_part2_2_variantX.ppm` per the spec note).
- `make part2_2_variant1` / `make part2_2_variant2` — Part II.2 (3 processes + pipes); creates `src/output_part2_2_variantX.ppm`.
- `make part2_3` — compiles and runs both variants of Part II.3 (3 processes + shared memory + semaphores); also available individually as `make part2_3_variant1` and `make part2_3_variant2`.
- `make clean` — removes `.out` binaries and `output_*.ppm` artifacts.

