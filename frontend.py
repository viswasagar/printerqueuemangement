import tkinter as tk
from tkinter import ttk, messagebox
from tkinter.scrolledtext import ScrolledText
import subprocess
import threading

# Global list to hold the job tuples: (job_id, pages, category, priority)
job_list = []

def add_job():
    job_id = entry_job_id.get().strip()
    pages = entry_pages.get().strip()
    category = category_var.get().strip()
    priority = priority_var.get().strip()

    if not job_id or not pages or not category or not priority:
        messagebox.showwarning("Input Error", "All fields must be filled!")
        return

    try:
        job_id_int = int(job_id)
        pages_int = int(pages)
    except ValueError:
        messagebox.showerror("Input Error", "Job ID and Pages must be numbers!")
        return

    try:
        # The combo boxes are expected to start with a number
        category_num = int(category.split()[0])
        priority_num = int(priority.split()[0])
    except Exception:
        messagebox.showerror("Input Error", "Category and Priority should start with a number.")
        return

    job = (job_id_int, pages_int, category_num, priority_num)
    job_list.append(job)
    listbox_jobs.insert(tk.END, f"ID:{job_id_int} | Pages:{pages_int} | Type:{category_num} | Priority:{priority_num}")

    # Clear entries for next input.
    entry_job_id.delete(0, tk.END)
    entry_pages.delete(0, tk.END)
    category_var.set('')
    priority_var.set('')

def append_output(text):
    """Append text to the output window safely."""
    def inner():
        output_text.insert(tk.END, text)
        output_text.see(tk.END)
    root.after(0, inner)

def execute_backend(jobs_to_submit):
    """
    Prepares job submission data from a list of jobs and executes the backend.
    jobs_to_submit should be a list of tuples (job_id, pages, category, priority).
    """
    if not jobs_to_submit:
        messagebox.showwarning("No Job", "No job to submit!")
        return

    # Format input data for the backend:
    # First line: number of jobs
    input_data = f"{len(jobs_to_submit)}\n"
    for job in jobs_to_submit:
        # Each job is provided in four lines.
        input_data += f"{job[0]}\n{job[1]}\n{job[2]}\n{job[3]}\n"

    try:
        process = subprocess.Popen(
            ["printer_queue.exe"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding='utf-8',
            errors='replace'
        )

        def read_output(pipe):
            for line in iter(pipe.readline, ''):
                append_output(line)
            pipe.close()

        # Launch threads to capture the backend's output.
        threading.Thread(target=read_output, args=(process.stdout,), daemon=True).start()
        threading.Thread(target=read_output, args=(process.stderr,), daemon=True).start()

        process.stdin.write(input_data)
        process.stdin.close()
        process.wait()

    except Exception as e:
        root.after(0, lambda: messagebox.showerror("Execution Error", str(e)))

def submit_job():
    """Submit a single selected job from the listbox."""
    selected_indices = listbox_jobs.curselection()
    if not selected_indices:
        messagebox.showwarning("Selection Error", "Please select a job to submit!")
        return

    index = selected_indices[0]
    job = job_list.pop(index)
    listbox_jobs.delete(index)
    threading.Thread(target=execute_backend, args=([job],), daemon=True).start()

def submit_all_jobs():
    """Submit all jobs currently in the GUI queue."""
    if not job_list:
        messagebox.showwarning("No Jobs", "No jobs to submit!")
        return

    jobs_to_submit = job_list.copy()  # Create a copy of the jobs to be submitted.
    job_list.clear()
    listbox_jobs.delete(0, tk.END)
    threading.Thread(target=execute_backend, args=(jobs_to_submit,), daemon=True).start()

# ------------------------- Tkinter Layout -------------------------

root = tk.Tk()
root.title("Printer Queue Management")

# Use zoomed state if available; else set size manually.
try:
    root.state('zoomed')
except Exception:
    root.geometry("1200x800")

main_frame = ttk.Frame(root, padding="10")
main_frame.grid(row=0, column=0, sticky="nsew")
root.columnconfigure(0, weight=1)
root.rowconfigure(0, weight=1)

main_frame.columnconfigure(0, weight=1)
main_frame.columnconfigure(1, weight=1)
# Increase the output row weight so it gets extra space.
main_frame.rowconfigure(9, weight=3)

# Input fields for new job entry.
ttk.Label(main_frame, text="Job ID:").grid(row=0, column=0, sticky="w", padx=5, pady=5)
entry_job_id = ttk.Entry(main_frame)
entry_job_id.grid(row=0, column=1, sticky="ew", padx=5, pady=5)

ttk.Label(main_frame, text="Pages:").grid(row=1, column=0, sticky="w", padx=5, pady=5)
entry_pages = ttk.Entry(main_frame)
entry_pages.grid(row=1, column=1, sticky="ew", padx=5, pady=5)

ttk.Label(main_frame, text="Category:").grid(row=2, column=0, sticky="w", padx=5, pady=5)
category_var = ttk.Combobox(main_frame,
                            values=["1 - Newspaper", "2 - Magazine/Book", "3 - Advertisement"])
category_var.grid(row=2, column=1, sticky="ew", padx=5, pady=5)

ttk.Label(main_frame, text="Priority:").grid(row=3, column=0, sticky="w", padx=5, pady=5)
priority_var = ttk.Combobox(main_frame, values=["1 - High", "2 - Medium", "3 - Low"])
priority_var.grid(row=3, column=1, sticky="ew", padx=5, pady=5)

btn_add_job = ttk.Button(main_frame, text="Add Job", command=add_job)
btn_add_job.grid(row=4, column=0, columnspan=2, pady=10)

# Display the list of added jobs (with a decreased textbox height).
ttk.Label(main_frame, text="Job Queue:").grid(row=5, column=0, columnspan=2, sticky="w", padx=5, pady=5)
frame_listbox = ttk.Frame(main_frame)
frame_listbox.grid(row=6, column=0, columnspan=2, sticky="nsew", padx=5, pady=5)
frame_listbox.columnconfigure(0, weight=1)
frame_listbox.rowconfigure(0, weight=1)
scrollbar_jobs = ttk.Scrollbar(frame_listbox, orient=tk.VERTICAL)
listbox_jobs = tk.Listbox(frame_listbox, height=5, yscrollcommand=scrollbar_jobs.set, font=("Consolas", 10))
scrollbar_jobs.config(command=listbox_jobs.yview)
scrollbar_jobs.grid(row=0, column=1, sticky="ns")
listbox_jobs.grid(row=0, column=0, sticky="nsew")

# Submission buttons: Submit Selected and Submit All.
submit_frame = ttk.Frame(main_frame)
submit_frame.grid(row=7, column=0, columnspan=2, pady=10)
btn_submit_job = ttk.Button(submit_frame, text="Submit Selected Job", command=submit_job)
btn_submit_job.pack(side=tk.LEFT, padx=5)
btn_submit_all = ttk.Button(submit_frame, text="Submit All Jobs", command=submit_all_jobs)
btn_submit_all.pack(side=tk.LEFT, padx=5)

# Output display area with increased height.
ttk.Label(main_frame, text="Output:").grid(row=8, column=0, columnspan=2, sticky="w", padx=5, pady=5)
output_text = ScrolledText(main_frame, height=30, font=("Consolas", 10))

output_text.grid(row=9, column=0, columnspan=2, sticky="nsew", padx=5, pady=5)

btn_clear_output = ttk.Button(main_frame, text="Clear Output", command=lambda: output_text.delete(1.0, tk.END))
btn_clear_output.grid(row=10, column=0, columnspan=2, pady=5)

root.mainloop()
