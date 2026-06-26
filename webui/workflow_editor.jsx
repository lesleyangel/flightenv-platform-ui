function WorkflowEditor({ object, workflowId, onWorkflowSaved, onWorkflowSelected, onWorkflowDeleted, readOnly = false }) {
  const hostRef = React.useRef(null);
  const [message, setMessage] = React.useState("");

  React.useEffect(() => {
    const host = hostRef.current;
    if (!host) return undefined;
    if (!window.FlightEnvReactFlowEditor || typeof window.FlightEnvReactFlowEditor.mount !== "function") {
      setMessage("React Flow 编辑器 bundle 未加载，请先运行 npm.cmd run build。");
      return undefined;
    }
    setMessage("");
    const mounted = window.FlightEnvReactFlowEditor.mount(host, { object, workflowId, onWorkflowSaved, onWorkflowSelected, onWorkflowDeleted, readOnly });
    return () => {
      if (mounted && typeof mounted.unmount === "function") mounted.unmount();
    };
  }, [object && object.object_id, workflowId, onWorkflowSaved, onWorkflowSelected, onWorkflowDeleted, readOnly]);

  return (
    <div className="workflow-reactflow-host">
      {message && <div className="empty"><strong>{message}</strong></div>}
      <div ref={hostRef} />
    </div>
  );
}

window.WorkflowEditor = WorkflowEditor;
